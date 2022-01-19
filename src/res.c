/*
 * AVANAC-NMS
 *
 * @author: Karim Khalifeh <khalifeh@ava.ir> - epilogue
 * @date : 27 aban 97
 * @copyright: ava.ir
 *
 */

#include <sys/param.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/queue.h>

#include <fcntl.h>

#include <kore/kore.h>
#include <kore/http.h>

#include "assets.h"

struct resource {
	int			fd;
	int			ref;
	off_t			size;
	char			*path;
	u_int8_t		*data;
	void			*base;

	TAILQ_ENTRY(resource)	list;
};

int		res_reader(struct http_request *);

static void	resource_unmap(struct resource *);
static int	res_reader_finish(struct netbuf *);
static int	resource_mmap(struct http_request *, struct resource *);
static int	resource_open(const char *fpath, struct http_request *, struct resource **);

TAILQ_HEAD(, resource)		resources;

int init_res(int state) {
	TAILQ_INIT(&resources);
	return (KORE_RESULT_OK);
}

int res_reader(struct http_request *req) {
	struct resource	*v;
	const char	*header;
	off_t		start, end;
	int			n, err, status;
	char		*bytes, *range[3], rb[128], *ext=NULL, ctype[32], *mime_type = "text/plain";
	char		fpath[MAXPATHLEN];

	kore_log(LOG_NOTICE,"res path:[%s]", req->path );
	if( !req->path[0] || (req->path[0]=='/' && !req->path[1]) ){ // default page
		if (!kore_snprintf(fpath, sizeof(fpath), NULL, "%s", "res/index.html" ) ){
			http_response(req, 500, NULL, 0);
			return (KORE_RESULT_OK);
		}
	}else{
		char *pp = req->path+(req->path[0]=='/' ? 1 : 0);/* skip first slash if exists */
		char *tp = "res";
		// if( req->path[0]=='/' && req->path[1]=='m' && req->path[2]=='a' && req->path[3]=='p' && req->path[4]=='/' ){ // map
		// 	tp = p_global_data->args.path_maps_wwwroot;
		// 	pp+=4;
		// }
		if (!kore_snprintf(fpath, sizeof(fpath), NULL, "%s/%s", tp, pp ) ){
			http_response(req, 500, NULL, 0);
			return (KORE_RESULT_OK);
		}
	}

	kore_log(LOG_NOTICE,"res path:[%s]==>[%s]", req->path, fpath );
	if ( (err=resource_open(fpath, req, &v)) ){
		kore_log(LOG_NOTICE,"res not found");
		http_response(req, err, NULL, 0);
		return (KORE_RESULT_OK);
	}

	kore_log(LOG_NOTICE,"res ll:%s", strrchr ("hello, world", 'l'));
	ext = strrchr(fpath, '.');
	if (ext == NULL || *(ext+1)==0 ) {
		v->ref--;
		http_response(req, 400, NULL, 0);
		return (KORE_RESULT_OK);
	}

	ext+=1;
	kore_log(LOG_NOTICE,"res ext:%s", ext);

	if( strcmp(ext, "png") == 0 ){
		mime_type = "image/png";

	}else if( strcmp(ext, "jpeg") == 0 || strcmp(ext, "jpg") == 0 ){
		mime_type = "image/jpeg";

	}else if( strcmp(ext, "htm") == 0 || strcmp(ext, "html") == 0 ){
		mime_type = "text/html";

	}else if( strcmp(ext, "css") == 0 ){
		mime_type = "text/css";

	}else if( strcmp(ext, "js") == 0 ){
		mime_type = "text/javascript";

	}else if( strcmp(ext, "txt") ){
		mime_type = "application/octet-stream";
	}

	if (!kore_snprintf(ctype, sizeof(ctype), NULL, mime_type, ext + 1)) {
		v->ref--;
		http_response(req, 500, NULL, 0);
		return (KORE_RESULT_OK);
	}

	kore_log(LOG_NOTICE, "%p: opened %s (%s) for streaming (%ld ref:%d)", (void *)req->owner, v->path, ctype, v->size, v->ref);

	if (http_request_header(req, "range", &header)) {
		if ((bytes = strchr(header, '=')) == NULL) {
			v->ref--;
			http_response(req, 416, NULL, 0);
			return (KORE_RESULT_OK);
		}

		bytes++;
		n = kore_split_string(bytes, "-", range, 3);
		if (n == 0) {
			v->ref--;
			http_response(req, 416, NULL, 0);
			return (KORE_RESULT_OK);
		}

		if (n >= 1) {
			start = kore_strtonum64(range[0], 1, &err);
			if (err != KORE_RESULT_OK) {
				v->ref--;
				http_response(req, 416, NULL, 0);
				return (KORE_RESULT_OK);
			}
		}

		if (n > 1) {
			end = kore_strtonum64(range[1], 1, &err);
			if (err != KORE_RESULT_OK) {
				v->ref--;
				http_response(req, 416, NULL, 0);
				return (KORE_RESULT_OK);
			}
		} else {
			end = 0;
		}

		if (end == 0)
			end = v->size;

		if (start > end || start > v->size || end > v->size) {
			v->ref--;
			http_response(req, 416, NULL, 0);
			return (KORE_RESULT_OK);
		}

		status = 206;
		if (!kore_snprintf(rb, sizeof(rb), NULL,
		    "bytes %ld-%ld/%ld", start, end - 1, v->size)) {
			v->ref--;
			http_response(req, 500, NULL, 0);
			return (KORE_RESULT_OK);
		}

		kore_log(LOG_NOTICE, "%p: %s sending: %ld-%ld/%ld", (void *)req->owner, v->path, start, end - 1, v->size);
		http_response_header(req, "content-range", rb);
	} else {
		start = 0;
		status = 200;
		end = v->size;
	}

	http_response_header(req, "content-type", ctype);
	http_response_header(req, "accept-ranges", "bytes");
	http_response_stream(req, status, v->data + start,
	    end - start, res_reader_finish, v);

	return (KORE_RESULT_OK);
}

static int resource_open(const char *fpath, struct http_request *req, struct resource **out) {
	struct stat		st;
	struct resource		*v;

	TAILQ_FOREACH(v, &resources, list) {
		if (!strcmp(v->path, fpath)) {
			if (resource_mmap(req, v)) {
				kore_log(LOG_NOTICE,"res found[%s]", v->path );
				*out = v;
				return 0;
			}

			close(v->fd);
			TAILQ_REMOVE(&resources, v, list);
			kore_free(v->path);
			kore_free(v);

			return 500;
		}
	}

	v = kore_malloc(sizeof(*v));
	v->ref = 0;
	v->base = NULL;
	v->data = NULL;
	v->path = kore_strdup(fpath);

	kore_log(LOG_NOTICE,"res opening:[%s]", fpath );
	if ((v->fd = open(fpath, O_RDONLY)) == -1) {
		kore_free(v->path);
		kore_free(v);
		return (errno == ENOENT) ? 404 : 500;
	}
	if (fstat(v->fd, &st) == -1) {
		close(v->fd);
		kore_free(v->path);
		kore_free(v);
		return 500;
	}
	v->size = st.st_size;
	if (!resource_mmap(req, v)) {
		close(v->fd);
		kore_free(v->path);
		kore_free(v);
		return 500;
	}
        kore_log(LOG_NOTICE,"f" );
	*out = v;
        kore_log(LOG_NOTICE,"ff" );
	TAILQ_INSERT_TAIL(&resources, v, list);
        kore_log(LOG_NOTICE,"g" );
	return 0;
}

static int resource_mmap(struct http_request *req, struct resource *v) {
	if (v->base != NULL && v->data != NULL) {
		v->ref++;
		return (KORE_RESULT_OK);
	}

	v->base = mmap(NULL, v->size, PROT_READ, MAP_SHARED, v->fd, 0);
	if (v->base == MAP_FAILED)
		return (KORE_RESULT_ERROR);

	v->ref++;
	v->data = v->base;

	return (KORE_RESULT_OK);
}

static int res_reader_finish(struct netbuf *nb) {
	struct resource	*v = nb->extra;

	v->ref--;
	kore_log(LOG_NOTICE,"%p: resource stream %s done (%zu/%zu ref:%d)",
	    (void *)nb->owner, v->path, nb->s_off, nb->b_len, v->ref);

	if (v->ref == 0)
		resource_unmap(v);

	return (KORE_RESULT_OK);
}

static void resource_unmap(struct resource *v) {
	if (munmap(v->base, v->size) == -1) {
		kore_log(LOG_NOTICE,"munmap(%s): %s", v->path, errno_s);
	} else {
		v->base = NULL;
		v->data = NULL;
		kore_log(LOG_NOTICE,"unmapped %s for streaming, no refs left", v->path);
	}
}
