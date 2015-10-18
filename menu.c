#define _GNU_SOURCE
#include <stdlib.h>
#include <vigor.h>

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>

#define TITLE_NAME_BUFSIZ 256

#define TITLE_TYPE_UNKNOWN 0
#define TITLE_TYPE_SNES    1
#define TITLE_TYPE_NES     2

typedef struct {
	char         *path;
	unsigned int  type;

	struct {
		char *title;
		char *developer;
		char *publisher;
		char *released;
	} metadata;

	SDL_Surface  *box_inset;
	SDL_Surface  *box_overlay;
	SDL_Rect      coords;

	list_t        staging;
} title_t;

typedef struct {
	int width;   /* how many titles can fit in a single row? */
	int gutter;  /* number of pixels between each title */
	int margin;  /* number of pixels between grid and edge of screen */

	int length;  /* how many titles are there total? */
	int current; /* index of currently-selected title */
	title_t **titles;

	SDL_Rect box_rect, graphic_rect;
	SDL_Surface *box;
	SDL_Surface *overlay;
} title_grid_t;

SDL_Surface* load_png(const char *path, SDL_Surface *optimize_for)
{
	SDL_Surface *raw = IMG_Load(path);
	if (!optimize_for)
		return raw;

	SDL_Surface *opt = SDL_ConvertSurface(raw, optimize_for->format, 0);
	SDL_FreeSurface(raw);
	return opt;
}

title_t* title_read_from_metadata(char *root)
{
	char *path = string("%s/.title", root);
	FILE *io = fopen(path, "r");
	if (!io) {
		fprintf(stderr, "%s: not readable\n", path);
		free(path);
		return NULL;
	}

	title_t *title = vmalloc(sizeof(title_t));
	title->path = root;

	unsigned int line = 0;
	char buf[8192], *key, *value, *a, *b;
	while (fgets(buf, 8191, io) != NULL) {
		line++;
		for (a = buf; *a && isspace(*a); a++) ; // leading whitespace
		if (!*a || *a == '#') continue;         // blank lines and comments

		for (b = a; *b && !isspace(*b); b++) ;  // first token - key;
		if (!*b) {
			fprintf(stderr, "%s: malformed entry on line %u; skipping\n", path, line);
			continue;
		}
		key = a;
		*b++ = '\0';

		for (a = b; *a && isspace(*a); a++) ;  // skip whitespace
		for (b = a; *b && *b != '\n'; b++) ;   // rest of line - value;
		if (*b != '\n') {
			fprintf(stderr, "%s: malformed entry on line %u (line too long?); skipping\n", path, line);
			continue;
		}
		value = a;
		*b++ = '\0';

		if (strcasecmp(key, "title") == 0) {
			free(title->metadata.title);
			title->metadata.title = strdup(value);

		} else if (strcasecmp(key, "developer") == 0) {
			free(title->metadata.developer);
			title->metadata.developer = strdup(value);
		} else if (strcasecmp(key, "publisher") == 0) {
			free(title->metadata.publisher);
			title->metadata.publisher = strdup(value);

		} else if (strcasecmp(key, "released") == 0) {
			free(title->metadata.released);
			title->metadata.released = strdup(value);

		} else if (strcasecmp(key, "inset") == 0) {
			SDL_FreeSurface(title->box_inset);
			SDL_FreeSurface(title->box_overlay);
			title->box_overlay = NULL;
			char *path = string("%s/%s", title->path, value);
			title->box_inset = load_png(path, NULL);
			free(path);

		} else if (strcasecmp(key, "overlay") == 0) {
			SDL_FreeSurface(title->box_overlay);
			SDL_FreeSurface(title->box_inset);
			title->box_inset = NULL;
			char *path = string("%s/%s", title->path, value);
			title->box_overlay = load_png(path, NULL);
			free(path);

		} else {
			fprintf(stderr, "%s: unrecognized key `%s' on line %u; skipping\n", path, key, line);
			continue;
		}
	}
	fclose(io);
	free(path);
	return title;
}

title_grid_t* title_scanfs(const char *root)
{
	char *path = string("%s/.index", root);
	FILE *io = fopen(path, "r");
	if (!io) {
		free(path);
		return NULL;
	}

	LIST(titles);
	title_t *title = NULL;
	unsigned int n = 0;
	unsigned int line = 0;
	char buf[8192], *a, *b;
	while (fgets(buf, 8191, io) != NULL) {
		line++;
		for (a = buf; *a && isspace(*a); a++) ; // leading whitespace
		if (!*a || *a == '#') continue;         // blank lines and comments

		for (b = a; *b && *b != '\n'; b++) ;    // rest of line - value;
		if (*b != '\n') {
			fprintf(stderr, "%s: malformed entry on line %u (line too long?); skipping\n", path, line);
			continue;
		}
		*b++ = '\0';

		fprintf(stderr, "checking title %s/%s\n", root, a);
		title = title_read_from_metadata(string("%s/%s", root, a));
		if (title) {
			list_push(&titles, &title->staging);
			n++;
		}
	}
	fclose(io);
	free(path);

	title_grid_t *grid = vmalloc(sizeof(title_grid_t));
	grid->titles = vcalloc(n, sizeof(title_t*));
	grid->length = n;
	n = 0;
	for_each_object(title, &titles, staging) {
		grid->titles[n++] = title;
	}

	return grid;
}

int init_grid(title_grid_t *grid)
{
	grid->overlay = load_png("assets/overlay.png", NULL);

	grid->box = load_png("assets/snes.png", NULL);
	grid->box_rect.x = grid->box_rect.y = 0;
	grid->box_rect.w = grid->box->w;
	grid->box_rect.h = grid->box->h;

	grid->graphic_rect.x = 0;
	grid->graphic_rect.y = 18;
	grid->graphic_rect.w = 328;
	grid->graphic_rect.h = 230;

	/* FIXME: determine how to better calculate optimal gutter / margin values */
	grid->gutter = 20;
	grid->width  = 1280 / (grid->box->w + grid->gutter);
	grid->margin = (1280 - (grid->box->w * grid->width) - (grid->gutter * (grid->width - 1))) / 2;

	return 0;
}

int draw_title(SDL_Surface *scr, title_grid_t *grid, SDL_Rect *offset, title_t *title)
{
	int y = offset->y;
	SDL_BlitSurface(grid->box, NULL, scr, offset);
	offset->y = y;

	SDL_Rect target, clip;

	if (title->box_inset) {
		target.x = offset->x + grid->graphic_rect.x;
		target.y = offset->y + grid->graphic_rect.y;
		target.w =             grid->graphic_rect.w;
		target.h =             grid->graphic_rect.h;

		clip.x = 0;
		clip.y = 0;
		clip.w = target.w;
		clip.h = target.h;

		y = target.y;
		SDL_FillRect(scr, &target, SDL_MapRGBA(scr->format, 20, 20, 20, 255));
		target.y = y;

		SDL_BlitSurface(title->box_inset, &clip, scr, &target);
		return 0;
	}

	if (title->box_overlay) {
		target.x = offset->x;
		target.y = offset->y;
		target.w = grid->box_rect.w;
		target.h = grid->box_rect.h;

		clip.x = 0;
		clip.y = 0;
		clip.w = target.w;
		clip.h = target.h;

		SDL_BlitSurface(title->box_overlay, &clip, scr, &target);
		return 0;
	}

	TTF_Font *font = TTF_OpenFont("assets/snes.ttf", 48);
	if (font) {
		SDL_Color fg = { 255, 255, 255 };
		char *a, *s = strdup(title->metadata.title);
		for (a = s; *a; *a = toupper(*a), a++) ;
		title->box_inset = TTF_RenderText_Solid(font, s, fg);
		free(s);
		TTF_CloseFont(font);

		if (title->box_inset)
			return draw_title(scr, grid, offset, title);
	}

	fprintf(stderr, "failed to render");
	return 1;
}

int draw_grid(SDL_Surface *scr, title_grid_t *grid)
{
	SDL_Rect off = { grid->margin, 0, 0, 0 };

	SDL_FillRect(scr, NULL, SDL_MapRGBA(scr->format, 128, 128, 128, 255));

	/* find the top of the current row of boxes */
	int top = (768 - grid->box->h) / 2;

	/* draw row with "current" selection in it. */
	int w = grid->current % grid->width;
	off.x = grid->margin - 4 + w * (grid->box->w + grid->gutter);
	off.y = top - 4;
	off.h = grid->box->h + 4 + 4;
	off.w = grid->box->w + 4 + 4;
	SDL_FillRect(scr, &off, SDL_MapRGBA(scr->format, 255, 0, 255, 255));

	off.x = grid->margin;
	off.y = top;
	int ii;
	for (ii = 0; ii < grid->width; ii++) {
		draw_title(scr, grid, &off, grid->titles[3 + ii]);
		off.x += grid->box->w + grid->gutter;
	}

	off.x = grid->margin;
	off.y -= grid->box->h + grid->gutter;
	for (ii = 0; ii < grid->width; ii++) {
		draw_title(scr, grid, &off, grid->titles[0 + ii]);
		off.x += grid->box->w + grid->gutter;
	}

	off.x = grid->margin;
	off.y += 2 * (grid->box->h + grid->gutter);
	for (ii = 0; ii < grid->width; ii++) {
		draw_title(scr, grid, &off, grid->titles[6 + ii]);
		off.x += grid->box->w + grid->gutter;
	}

	SDL_BlitSurface(grid->overlay, NULL, scr, NULL);
	return 0;
}

int main(int argc, char **argv)
{
	if (SDL_Init(SDL_INIT_VIDEO) != 0) {
		fprintf(stderr, "SDL: %s\n", SDL_GetError());
		return 1;
	}

	SDL_Surface *scr;
	SDL_Event ev;

	scr = SDL_SetVideoMode(1280, 768, 0, SDL_SWSURFACE|SDL_DOUBLEBUF);
	if (!scr) {
		fprintf(stderr, "video mode: %s\n", SDL_GetError());
		return 1;
	}
	if ( !(IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG) ) {
		fprintf(stderr, "png lib: %s\n", IMG_GetError());
		SDL_Quit();
		return 1;
	}

	if (TTF_Init() != 0) {
		fprintf(stderr, "ttf lib: %s\n", TTF_GetError());
		IMG_Quit();
		TTF_Quit();
		return 1;
	}

	title_grid_t *grid = title_scanfs("./root");
	if (grid->length != 9) {
		fprintf(stderr, "not enough titles found for demo!\n");
		return 1;
	}
	init_grid(grid);

	int loop = 1;
	while (loop) {
		while (SDL_PollEvent(&ev))
			if (ev.type == SDL_QUIT)
				loop = 0;

		draw_grid(scr, grid);
		SDL_Flip(scr);
	}

	TTF_Quit();
	IMG_Quit();
	SDL_Quit();
	free(grid);
	return 0;
}
