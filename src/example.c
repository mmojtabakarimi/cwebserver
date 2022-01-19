/*
 * Copyright (c) 2013 Joris Vink <joris@coders.se>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <kore/kore.h>
#include <kore/http.h>

#include <openssl/sha.h>

#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include "assets.h"
int init_done = 0;
int		init_res(int);

int init(int state) {

	if (state == KORE_MODULE_UNLOAD) {
	        kore_log(LOG_NOTICE, "module unloading");
		return (KORE_RESULT_ERROR);
	}

	if( init_done ){
		return (KORE_RESULT_OK);
	}

	kore_log(LOG_NOTICE, "module loading");
	init_res(state);

	return (KORE_RESULT_OK);
}

void kore_worker_configure(void){
        kore_log(LOG_NOTICE, "------------------------------kore_worker_configure ");
	init(KORE_MODULE_LOAD);
	init_done = 1;
}

int
show_index(struct http_request *req)
{
	int			i;
	size_t			len;
	struct kore_buf		*res;
	u_int8_t		*data;
	char * param = NULL;

        res = kore_buf_alloc(1024);
	
	http_populate_get(req);
	http_argument_get_string(req, "arg", &param);

/*
	FILE *fd = fopen("index.html");
	char content[4096];
	content[4095] = 0;
	size_t rlen = 0;
	while ((rlen = fread (&content, 1, 4095, fd)) > 0) {
		kore_buf_append(res, content, rlen);
	}
	fclose(fd);
*/

	kore_buf_appendf(res, "<html>");
        kore_buf_appendf(res, "<body>");
        kore_buf_appendf(res, "ARG:%s", param ? param : "nothing");
        kore_buf_appendf(res, "</body>");
        kore_buf_appendf(res, "</html>");

	http_response_header(req, "content-type", "text/html");
	http_response(req, 200, res->data, res->offset);
	kore_buf_free(res);

	return (KORE_RESULT_OK);
}

