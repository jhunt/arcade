#define _GNU_SOURCE
#include <stdlib.h>
#include <vigor.h>

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>

#define TITLE_FONT_SIZE 48

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
	int width;  /* how many titles can fit in a single row? */
	int gutter; /* number of pixels between each title */
	int margin; /* number of pixels between grid and edge of screen */

	struct {
		int width;
		int R, G, B, A;
	} highlight; /* how to highlight current title */

	int length;  /* how many titles are there total? */
	int current; /* index of currently-selected title */
	title_t **titles;

	SDL_Rect box_rect, inset_rect;
	SDL_Surface *box;
	SDL_Surface *overlay;
	TTF_Font    *font;
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

title_t* title_read_from_metadata(const char *root, const char *dir)
{
	title_t *title = vmalloc(sizeof(title_t));
	title->path = string("%s/%s", root, dir);;

	char *path = string("%s/%s/.title", root, dir);
	FILE *io = fopen(path, "r");
	if (!io) {
		fprintf(stderr, "%s: not readable\n", path);

		title->metadata.title = strdup(dir);
		free(path);
		return title;
	}


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
			fprintf(stderr, "loading inset from %s\n", path);
			title->box_inset = load_png(path, NULL);
			if (!title->box_inset) {
				fprintf(stderr, "%s: failed to load inset; skipping\n", path);
			}
			free(path);

		} else if (strcasecmp(key, "overlay") == 0) {
			SDL_FreeSurface(title->box_overlay);
			SDL_FreeSurface(title->box_inset);
			title->box_inset = NULL;

			char *path = string("%s/%s", title->path, value);
			fprintf(stderr, "loading overlay from %s\n", path);
			title->box_overlay = load_png(path, NULL);
			if (!title->box_overlay) {
				fprintf(stderr, "%s: failed to load overlay; skipping\n", path);
			}
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
	title_grid_t *grid = vmalloc(sizeof(title_grid_t));
	grid->overlay = load_png("assets/overlay.png", NULL);
	grid->gutter  = 10;

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
	char buf[8192], *a, *b, *key;
	while (fgets(buf, 8191, io) != NULL) {
		line++;
		for (a = buf; *a && isspace(*a); a++) ; // leading whitespace
		if (!*a || *a == '#') continue;         // blank lines and comments

		for (b = a; *b && !isspace(*b); b++) ;  // first token - key
		if (!*b || *b == '\n') {
			fprintf(stderr, "%s:%u: malformed entry; skipping\n", path, line);
			continue;
		}
		key = a;
		*b++ = '\0';

		for (a = b; *a && isspace(*a); a++) ;   // separating whitespace
		if (!*a) {
			fprintf(stderr, "%s:%u: malformed entry; skipping\n", path, line);
			continue;
		}

		for (b = a; *b && *b != '\n'; b++) ;    // rest of line - value;
		if (*b != '\n') {
			fprintf(stderr, "%s:%u: malformed entry; skipping\n", path, line);
			continue;
		}
		*b++ = '\0';

		if (strcasecmp(key, "BOX") == 0) {
			char *box_path = string("%s/%s", root, a);
			SDL_FreeSurface(grid->box);
			grid->box = load_png(box_path, NULL);
			if (!grid->box) {
				fprintf(stderr, "%s:%u: box image %s not found; aborting\n", path, line, box_path);
				free(box_path);
				goto bail;
			}
			free(box_path);
			grid->box_rect.x = grid->box_rect.y = 0;
			grid->box_rect.w = grid->box->w;
			grid->box_rect.h = grid->box->h;

		} else if (strcasecmp(key, "OVERLAY") == 0) {
			char *overlay_path = string("%s/%s", root, a);
			SDL_FreeSurface(grid->overlay);
			grid->overlay = load_png(overlay_path, NULL);
			free(overlay_path);

		} else if (strcasecmp(key, "FONT") == 0) {
			char *font_path = string("%s/%s", root, a);
			grid->font = TTF_OpenFont(font_path, TITLE_FONT_SIZE);
			free(font_path);

		} else if (strcasecmp(key, "INSET") == 0) {
			int args = 0;
			int sp = 1;
			for (b = a; isdigit(*b) || isspace(*b); b++) { // only allow numbers and whitespace
				if (sp && isdigit(*b)) {
					sp = 0;
					args++;
				} else if (!sp && isspace(*b)) {
					sp = 1;
				}
			}
			if (*b || args != 4) {
				fprintf(stderr, "%s:%u: inset requires four integer arguments - `inset x y width height'; skipping\n", path, line);
				continue;
			}

			int x = 0, y = 0, w = 0, h = 0;
			for (b = a; isdigit(*b); b++) x = x * 10 + (*b - '0');
			for (a = b; isspace(*a); a++) ;
			for (b = a; isdigit(*b); b++) y = y * 10 + (*b - '0');
			for (a = b; isspace(*a); a++) ;
			for (b = a; isdigit(*b); b++) w = w * 10 + (*b - '0');
			for (a = b; isspace(*a); a++) ;
			for (b = a; isdigit(*b); b++) h = h * 10 + (*b - '0');

			fprintf(stderr, "setting inset rect to (%i,%i) %ix%i\n", x, y, w, h);
			grid->inset_rect.x = x;
			grid->inset_rect.y = y;
			grid->inset_rect.w = w;
			grid->inset_rect.h = h;

		} else if (strcasecmp(key, "GUTTER") == 0) {
			int g = 0;
			for (b = a; isdigit(*b); b++) g = g * 10 + (*b - '0');
			for (a = b; isspace(*a); a++) ;
			if (*a) {
				fprintf(stderr, "%s:%u: gutter value must be an integer; skipping\n", path, line);
				continue;
			}
			fprintf(stderr, "setting gutter to %i\n", g);
			grid->gutter = g;

		} else if (strcasecmp(key, "HIGHLIGHT") == 0) {
			int args = 0;
			int sp = 1;
			for (b = a; isdigit(*b) || isspace(*b); b++) { // only allow numbers and whitespace
				if (sp && isdigit(*b)) {
					sp = 0;
					args++;
				} else if (!sp && isspace(*b)) {
					sp = 1;
				}
			}
			if (*b || args != 5) {
				fprintf(stderr, "%s:%u: highlight requires five integer arguments - `highlight width R G B A'; skipping\n", path, line);
				continue;
			}

			int w = 0, R = 0, G = 0, B = 0, A = 0;
			for (b = a; isdigit(*b); b++) w = w * 10 + (*b - '0');
			for (a = b; isspace(*a); a++) ;
			for (b = a; isdigit(*b); b++) R = R * 10 + (*b - '0');
			for (a = b; isspace(*a); a++) ;
			for (b = a; isdigit(*b); b++) G = G * 10 + (*b - '0');
			for (a = b; isspace(*a); a++) ;
			for (b = a; isdigit(*b); b++) B = B * 10 + (*b - '0');
			for (a = b; isspace(*a); a++) ;
			for (b = a; isdigit(*b); b++) A = A * 10 + (*b - '0');

			fprintf(stderr, "setting highlight to %i wide, rgba(%i,%i,%i,%i)\n", w, R, G, B, A);
			grid->highlight.width = w;
			grid->highlight.R     = R;
			grid->highlight.G     = G;
			grid->highlight.B     = B;
			grid->highlight.A     = A;

		} else if (strcasecmp(key, "GAME") == 0) {
			fprintf(stderr, "checking title %s/%s\n", root, a);
			title = title_read_from_metadata(root, a);
			if (title) {
				list_push(&titles, &title->staging);
				n++;
			}
		} else {
			fprintf(stderr, "%s:%u: unrecognized key `%s'; skipping\n", path, line, key);
			continue;
		}
	}

	if (!grid->overlay) {
		grid->overlay = load_png("assets/overlay.png", NULL);
	}
	if (!grid->font) {
		grid->font = TTF_OpenFont("assets/snes.ttf", TITLE_FONT_SIZE);
	}
	if (!grid->box) {
		fprintf(stderr, "%s: no box cover art template specified; aborting\n", path);
		goto bail;
	}

	grid->width  = 1280 / (grid->box->w + grid->gutter);
	grid->margin = (1280 - (grid->box->w * grid->width) - (grid->gutter * (grid->width - 1))) / 2;

	grid->titles = vcalloc(n, sizeof(title_t*));
	grid->length = n;
	n = 0;
	for_each_object(title, &titles, staging) {
		int row = n / grid->width;
		int col = n % grid->width;
		title->coords.x = col * (grid->box->w + grid->gutter);
		title->coords.y = row * (grid->box->h + grid->gutter) + grid->gutter;

		grid->titles[n++] = title;

		if (!title->box_inset && !title->box_overlay && grid->font) {
			char *a, *s = strdup(title->metadata.title);
			for (a = s; *a; *a = toupper(*a), a++) ;

			SDL_Color fg = { 255, 255, 255 };
			title->box_inset = TTF_RenderText_Solid(grid->font, s, fg);

			free(s);
		}
	}

	fclose(io);
	free(path);
	return grid;

bail:
	fclose(io);
	free(path);
	free(grid);
	return NULL;
}

int draw_title(SDL_Surface *scr, title_grid_t *grid, SDL_Rect *offset, title_t *title)
{
	int y = offset->y;
	SDL_BlitSurface(grid->box, NULL, scr, offset);
	offset->y = y;

	SDL_Rect target, clip;

	if (title->box_inset) {
		target.x = offset->x + grid->inset_rect.x;
		target.y = offset->y + grid->inset_rect.y;
		target.w =             grid->inset_rect.w;
		target.h =             grid->inset_rect.h;

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

	fprintf(stderr, "%s: failed to render (no overlay and no inset graphic)\n", title->path);
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
	off.x = grid->margin - grid->highlight.width + w * (grid->box->w + grid->gutter);
	off.y = top - grid->highlight.width;
	off.h = grid->box->h + 2 * grid->highlight.width;
	off.w = grid->box->w + 2 * grid->highlight.width;
	SDL_FillRect(scr, &off, SDL_MapRGBA(scr->format, grid->highlight.R, grid->highlight.G, grid->highlight.B, grid->highlight.A));

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
	if (!grid) {
		fprintf(stderr, "failed to initialize title grid\n");
		return 1;
	}
	if (grid->length != 9) {
		fprintf(stderr, "not enough titles found for demo!\n");
		return 1;
	}

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
