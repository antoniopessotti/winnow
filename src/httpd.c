// General info: http://doc.winnowtag.org/open-source
// Source code repository: http://github.com/winnowtag
// Questions and feedback: contact@winnowtag.org
//
// Copyright (c) 2007-2011 The Kaphan Foundation
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

// contact@winnowtag.org

#include "buffer.h"


#include "hmac_auth.h"


#include "tagger.h"


#include <config.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif

#include <time.h>
#include <sys/time.h>
#include <regex.h>
#include "httpd.h"
#include "item_cache.h"
#include "tagger.h"
#include "logging.h"
#include "misc.h"
#define _bits_h_
#include <json/json.h>

#define CONTENT_TYPE "application/xml"
#define NOT_FOUND "<?xml version='1.0' ?>\n<errors><error>Resource not found.</error></errors>\n"
#define METHOD_NOT_ALLOWED "<?xml version='1.0' ?>\n<errors><error>Method is not allowed for that resource.</errors></errors>\n"
#define BAD_XML "<?xml version='1.0' ?>\n<error><error>Badly formatted XML.</error></errors>\n"
#define MISSING_TAG_ID "<?xml version='1.0' ?>\n<errors><error>Missing tag or user id in job description</error></errors>\n"

#include <microhttpd.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>
#include <libxml/uri.h>
#include "xml_error_functions.h"

#include "xml.h"
#include "http_responses.h"

typedef enum HTTP_METHOD {
  GET,
  POST,
  PUT,
  DELETE
} HTTPMethod;

struct HTTPD {
  struct MHD_Daemon *mhd;
  HttpConfig *config;
  ClassificationEngine *ce;
  ItemCache *item_cache;
  TaggerCache *tagger_cache;
  regex_t start_job_regex;
  regex_t job_status_regex;
  regex_t about_regex;
  regex_t item_cache_create_feed_items_regex;
  regex_t item_cache_feed_items_regex;
  regex_t get_clues_regex;
};

typedef struct HTTP_REQUEST {
  HTTPMethod method;
  char * method_string;
  char *path;
  Buffer *data;
  struct MHD_Connection *connection;
  ClassificationEngine *ce;
  ItemCache *item_cache;
  TaggerCache *tagger_cache;
  struct timeval start_time;
} HTTPRequest;

typedef struct HTTP_RESPONSE {
  int code;
  char *content_type;
  char *location;
  char *content;
  int free_content;
} HTTPResponse;

static float tdiff(struct timeval from, struct timeval to) {
  double from_d = from.tv_sec + (from.tv_usec / 1000000.0);
  double to_d = to.tv_sec + (to.tv_usec / 1000000.0);
  return to_d - from_d;
}

/*  <job>
 *    <id>ID</id>
 *    <progress type="float">0.0</progress>
 *    <tag-id type="integer">N</tag-id>
 *  </job>
 */
static xmlChar * xml_for_job(const ClassificationJob *job) {
  xmlChar *buffer = NULL;
  int buffersize;
  xmlDocPtr doc = xmlNewDoc(BAD_CAST "1.0");
  xmlNodePtr root = xmlNewNode(NULL, BAD_CAST "job");
  xmlDocSetRootElement(doc, root);

  xmlNewChild(root, NULL, BAD_CAST "id", BAD_CAST job->id);

  if (CJOB_STATE_ERROR == job->state) {
    char buffer[256];
    xmlNewChild(root, NULL, BAD_CAST "error-message", BAD_CAST cjob_error_msg(job, buffer, sizeof(buffer)));
  }

  add_element(root, "duration", "float", "%.2f", cjob_duration(job));
  add_element(root, "progress", "float", "%.1f", job->progress);
  xmlNewChild(root, NULL, BAD_CAST "status", BAD_CAST cjob_state_msg(job));

  xmlDocDumpFormatMemory(doc, &buffer, &buffersize, 1);
  xmlFreeDoc(doc);

  return buffer;
}

static char * url_for_job(const ClassificationJob *job) {
  char *url = calloc(128, sizeof(char));
  snprintf(url, 128, "/classifier/jobs/%s", job->id);
  return url;
}

static char * extract_job_id(const char * url) {
  char *job_id = NULL;
  regex_t regex;
  regmatch_t matches[3];

  if (regcomp(&regex, "^/classifier/jobs/([A-Za-z0-9-]*)(.xml)?$", REG_EXTENDED)) {
    fatal("Error compiling regex");
    return NULL;
  }

  if (0 == regexec(&regex, url, 3, matches, 0)) {
    regmatch_t match = matches[1];
    job_id = calloc(match.rm_eo - match.rm_so + 1, sizeof(char));
    strncpy(job_id, &url[match.rm_so], match.rm_eo - match.rm_so);
  }

  regfree(&regex);
  return job_id;
}

static xmlChar * xml_for_about(void) {
  xmlChar *buffer = NULL;
  int buffersize;

  xmlDocPtr doc = xmlNewDoc(BAD_CAST "1.0");
  xmlNodePtr root = xmlNewNode(NULL, BAD_CAST "classifier");
  xmlDocSetRootElement(doc, root);

  add_element(root, "version", "string", "%s", PACKAGE_VERSION);

  xmlDocDumpFormatMemory(doc, &buffer, &buffersize, 1);
  xmlFreeDoc(doc);

  return buffer;
}

/********************************************************************************
 * HTTP Interface Handlers
 ********************************************************************************/

typedef int (*RequestHandler)(const HTTPRequest * request, HTTPResponse * response);

static HTTPMethod get_method(const char *method) {
  if (!strcmp(MHD_HTTP_METHOD_POST, method)) {
    return POST;
  } else if (!strcmp(MHD_HTTP_METHOD_PUT, method)) {
    return PUT;
  } else if (!strcmp(MHD_HTTP_METHOD_DELETE, method)) {
    return DELETE;
  } else {
    return GET;
  }
}

static int get_path_id(const char * pattern, const char * path) {
  int id = 0;
  regex_t regex;
  regmatch_t matches[2];

  if (regcomp(&regex, pattern, REG_EXTENDED)) {
    fatal("Error compiling regex");
    return -1;
  }

  if (0 == regexec(&regex, path, 2, matches, 0)) {
    regmatch_t match = matches[1];
    char *id_s = calloc(match.rm_eo - match.rm_so + 1, sizeof(char));
    strncpy(id_s, &path[match.rm_so], match.rm_eo - match.rm_so);
    id = strtol(id_s, NULL, 0);
    free(id_s);
  }

  regfree(&regex);

  return id;
}

static int get_entry_id(const char * path) {
  return get_path_id("^/feed_items/([0-9]+)$", path);
}

static int about_handler(const HTTPRequest * request, HTTPResponse * response) {
  response->code = MHD_HTTP_OK;
  response->content_type = CONTENT_TYPE;
  response->content = (char*) xml_for_about();
  response->free_content = MHD_YES;
  return 1;
}

static int add_entry(const HTTPRequest * request, HTTPResponse * response) {
  xmlDocPtr doc = NULL;

  if (POST != request->method) {
    response->code = MHD_HTTP_METHOD_NOT_ALLOWED;
    response->content = "<error>Only POST or PUT allowed</error>";
    response->content_type = "application/xml";
  } else if (NULL == request->data) {
    info("NO DATA");
    HTTP_BAD_XML(response);
  } else if (NULL == (doc = xmlReadMemory(request->data->buf, request->data->length, "", NULL, XML_PARSE_COMPACT))) {
    info("BAD DATA: %s", request->data->buf);
    HTTP_BAD_XML(response);
  } else {
    ItemCacheEntry *entry = create_entry_from_atom_xml_document(doc, request->data->buf);

    if (!entry) {
      HTTP_BAD_ENTRY(response);
    } else {
      if (CLASSIFIER_OK == item_cache_add_entry(request->item_cache, entry)) {
        response->code = MHD_HTTP_CREATED;
        response->content = strdup(request->data->buf);
        response->content_type = CONTENT_TYPE;
        response->location = calloc(64, sizeof(char));
        response->free_content = true;
        snprintf(response->location, 64, "/feed_items/%i", item_cache_entry_id(entry));
      } else {
        HTTP_BAD_ENTRY(response);
      }
      
      free_entry(entry);
    }
    
    xmlFreeDoc(doc);
  }

  return 0;
}

static int entry_handler(const HTTPRequest * request, HTTPResponse * response) {
  int entry_id = get_entry_id(request->path);

  if (entry_id <= 0) {
    HTTP_NOT_FOUND(response);
  } else if (DELETE == request->method) {
    int rc = item_cache_remove_entry(request->item_cache, entry_id);
    if (CLASSIFIER_OK == rc) {
      response->code = MHD_HTTP_NO_CONTENT;
      response->content = "<info>Entry removed successfully.</info>";
      response->content_type = CONTENT_TYPE;
    } else if (ITEM_CACHE_ENTRY_PROTECTED == rc) {
      response->code = MHD_HTTP_ACCEPTED;
      response->content = "<info>Entry removed successfully.</info>";
      response->content_type = CONTENT_TYPE;
    } else {
      HTTP_ISE(response);
    }
  } else if (PUT == request->method) {
  	response->code = MHD_HTTP_ACCEPTED;
		response->content = "";
  } else {
    HTTP_ISE(response);
  }

  return 0;
}

static int start_job(const HTTPRequest * request, HTTPResponse * response) {
  int ret = 0;

  if (request->method != POST) {
    response->code = MHD_HTTP_METHOD_NOT_ALLOWED;
    response->content = METHOD_NOT_ALLOWED;
    response->content_type = CONTENT_TYPE;
  } else if (!request->data || request->data->length <= 1) { // account for \0 in empty string
    response->code = MHD_HTTP_UNSUPPORTED_MEDIA_TYPE;
    response->content = BAD_XML;
    response->content_type = CONTENT_TYPE;
  } else {
    xmlDocPtr doc = xmlReadMemory(request->data->buf, request->data->length, "", NULL, XML_PARSE_COMPACT);

    if (doc == NULL) {
      HTTP_BAD_XML(response);
    } else {
      ClassificationJob *job = NULL;
      xmlXPathContextPtr context = xmlXPathNewContext(doc);
      xmlXPathObjectPtr result = xmlXPathEvalExpression(BAD_CAST "/job/tag-url/text()", context);

      if (!xmlXPathNodeSetIsEmpty(result->nodesetval)) {
        char * tag_url = (char*) result->nodesetval->nodeTab[0]->content;
        info("Starting classification job for tag %s", tag_url);
        job = ce_add_classification_job(request->ce, tag_url);
      } else {
        response->code = MHD_HTTP_UNPROCESSABLE_ENTITY;
        response->content = MISSING_TAG_ID;
        response->content_type = CONTENT_TYPE;
      }

      xmlXPathFreeObject(result);
      xmlXPathFreeContext(context);
      xmlFreeDoc(doc);

      if (job) {
        response->code = MHD_HTTP_CREATED;
        response->content_type = CONTENT_TYPE;
        response->content = (char*) xml_for_job(job);
        response->free_content = MHD_YES;
        response->location = url_for_job(job);
      }
    }
  }

  return ret;
}

static int get_clues_handler(const HTTPRequest * request, HTTPResponse * response) {
  Item *item = NULL;
  int free_when_done = 0;
  const char *item_id = MHD_lookup_connection_value(request->connection, MHD_GET_ARGUMENT_KIND, "item");
  const char *tag_url = MHD_lookup_connection_value(request->connection, MHD_GET_ARGUMENT_KIND, "tag");

  if (request->method != GET) {
    response->code = MHD_HTTP_METHOD_NOT_ALLOWED;
    response->content = METHOD_NOT_ALLOWED;
    response->content_type = CONTENT_TYPE;
  } else if (!(item_id && tag_url)) {
    response->code = MHD_HTTP_BAD_REQUEST;
    response->content_type = "text/plain";
    !item_id ? (response->content = "Missing item parameter") : (response->content = "Missing tag parameter");
  } else if (NULL == (item = item_cache_fetch_item(request->item_cache, item_id, &free_when_done))) {
    response->code = MHD_HTTP_NOT_FOUND;
    response->content_type = "text/plain";
    response->content = "The item does not exist in the classifier's item cache.";
  } else {
    Tagger *tagger = NULL;
    int get_tagger_rc = get_tagger_without_fetching(request->tagger_cache, tag_url, &tagger, NULL);

    switch (get_tagger_rc) {
      case TAG_NOT_FOUND:
        /* This means that the tag is not cached, but we need to check is_error to see if
         * a previous background fetch resulted in an error since our call to get_tagger
         * doesn't actually do the fetching.  The reason for this is that if the tag is not
         * cached we don't want our call to get_tagger to do the actual fetching because it
         * would block the HTTP thread and could result in deadlocks if the tag url points back to the
         * server which generated this request.  So instead we trigger the fetching of the
         * tag in another thread and return a redirect to the caller.
         */
        if (is_failed_tag(request->tagger_cache, tag_url)) {
          response->code = MHD_HTTP_NOT_FOUND;
          response->content = "";
        } else {
          fetch_tagger_in_background(request->tagger_cache, tag_url);
          response->code = MHD_HTTP_FAILED_DEPENDENCY;
          response->content = "The classifier needs to load the tag to perform this operation. Please try again later.";
        }
        break;
      case TAGGER_CHECKED_OUT:
        response->code = MHD_HTTP_FAILED_DEPENDENCY;
        response->content = "The classifier is waiting on the tagger to become available.  Please try again later.";
        break;
      case TAGGER_OK: {
          int num_clues = 0;
          int i;
          Clue ** clues = get_clues(tagger, item, &num_clues);
          struct json_object *clue_array = json_object_new_array();

          for (i = 1; i < num_clues; i++) {
            char * feature = item_cache_globalize(request->item_cache, clues[i]->token_id);
            if (!feature) {
              continue;
            }

            struct json_object *clue = json_object_new_object();
            json_object_object_add(clue, "clue", json_object_new_string(feature));
            json_object_object_add(clue, "prob", json_object_new_double(clues[i]->probability));
            json_object_array_add(clue_array, clue);
            free(feature);
          }

          response->code = MHD_HTTP_OK;
          response->content_type = "application/json";
          response->content = strdup(json_object_to_json_string(clue_array));
          response->free_content = true;

          free(clues);
          json_object_put(clue_array);
          release_tagger(request->tagger_cache, tagger);
        }

        break;
    }

    if (free_when_done) free_item(item);
  }

  return 1;
}

static int job_handler(const HTTPRequest * request, HTTPResponse * response) {
  int ret;
  char *job_id = extract_job_id(request->path);
  ClassificationJob *job = NULL;

  if (job_id == NULL) {
    HTTP_NOT_FOUND(response)
  } else if (NULL == (job = ce_fetch_classification_job(request->ce, job_id))) {
    HTTP_NOT_FOUND(response);
  } else if (CJOB_STATE_CANCELLED == job->state) {
    // Cancelled jobs are considered deleted to the outside world.
    HTTP_NOT_FOUND(response);
  } else if (GET == request->method) {
    xmlChar *xml = xml_for_job(job);
    response->code = MHD_HTTP_OK;
    response->content_type = CONTENT_TYPE;
    response->content = (char*) xml;
    response->free_content = MHD_YES;
  } else if (DELETE == request->method) {
    if (CJOB_STATE_COMPLETE == job->state || CJOB_STATE_ERROR == job->state) {
      ce_remove_classification_job(request->ce, job, true);
      free_classification_job(job);
    } else {
      cjob_cancel(job);
    }
    response->code = MHD_HTTP_OK; // Should really by 204, but need Rails 2.0.2 first
    response->content = "";
    response->content_type = NULL;
  } else {
    response->code = MHD_HTTP_METHOD_NOT_ALLOWED;
    response->content = METHOD_NOT_ALLOWED;
    response->content_type = CONTENT_TYPE;
  }

  if (job_id) {
    free(job_id);
  }

  return ret;
}

struct SLIST_CONTAINER {
  struct curl_slist *header;
};

int header_iterator(void *memo, enum MHD_ValueKind kind, const char *key, const char *value) {
  struct SLIST_CONTAINER *slist = (struct SLIST_CONTAINER*) memo;
  Buffer *buffer = new_buffer(256);
  buffer_in(buffer, key, strlen(key));
  buffer_in(buffer, ": ", 2);
  buffer_in(buffer, value, strlen(value));
  buffer_in(buffer, "\0", 1);
  slist->header = curl_slist_append(slist->header, buffer->buf);
  free_buffer(buffer);
  return MHD_YES;
}

static struct curl_slist * convert_headers_to_slist(HTTPRequest * request) {
  struct SLIST_CONTAINER slist;
  slist.header = NULL;
  MHD_get_connection_values(request->connection, MHD_HEADER_KIND, header_iterator, &slist);  
  return slist.header;
}

static int authenticated(const Credentials * credentials, const HTTPRequest * request) {
  struct curl_slist *curl_headers = convert_headers_to_slist(request);
  int authenticated =  hmac_auth(request->method_string, request->path, curl_headers, credentials);
  curl_slist_free_all(curl_headers);
  return authenticated;
}

static int handle_request(const Httpd * httpd, const HTTPRequest * request, HTTPResponse * response) {
  const Credentials * credentials = NULL;
  RequestHandler handler = NULL;
  
  if (0 == regexec(&httpd->start_job_regex,                           request->path, 0, NULL, 0)) {
    credentials = httpd->config->classification_credentials;
    handler = &start_job;
  } else if (0 == regexec(&httpd->job_status_regex,                   request->path, 0, NULL, 0)) {
    credentials = httpd->config->classification_credentials;
    handler = &job_handler;
  } else if (0 == regexec(&httpd->about_regex,                        request->path, 0, NULL, 0)) {
    handler = &about_handler;
  } else if (0 == regexec(&httpd->item_cache_create_feed_items_regex, request->path, 0, NULL, 0)) {
    credentials = httpd->config->item_cache_credentials;
    handler = &add_entry;
  } else if (0 == regexec(&httpd->item_cache_feed_items_regex,        request->path, 0, NULL, 0)) {
    credentials = httpd->config->item_cache_credentials;
    handler = &entry_handler;
  } else if (0 == regexec(&httpd->get_clues_regex,                    request->path, 0, NULL, 0)) {
    credentials = httpd->config->classification_credentials;
    handler = &get_clues_handler;
  } 
  
  if (handler == NULL) {
    HTTP_NOT_FOUND(response);
  } else if (valid_credentials(credentials) && !authenticated(credentials, request)) {
    HTTP_UNAUTHORIZED(response);
  } else {
    handler(request, response);
  }

  return 1;
}

/* This is the base response handler. It just returns a 404.
 *
 */
static int process_request(void * httpd_vp, struct MHD_Connection * connection,
                           const char * raw_url, const char * method,
                           const char * version, const char * upload_data,
                           unsigned int * upload_data_size, void **memo) {
  SET_XML_ERROR_HANDLERS;
  int new_request = false;
  int ret = MHD_YES;
  Httpd * httpd = (Httpd*) httpd_vp;

  HTTPRequest *request = (HTTPRequest*) *memo;
  if (NULL == request) {
    new_request = true;
    request = calloc(1, sizeof(HTTPRequest));
    request->method = get_method(method);
    request->method_string = method;
    request->connection = connection;
    request->ce = httpd->ce;
    request->item_cache = httpd->item_cache;
    request->tagger_cache = httpd->tagger_cache;
    request->path = raw_url;
    gettimeofday(&request->start_time, NULL);

    if (request->method == PUT || request->method == POST) {
      request->data = new_buffer(*upload_data_size);
    }

    *memo = request;
  }

  if (*upload_data_size > 0) {
    buffer_in(request->data, upload_data, *upload_data_size);
    *upload_data_size = 0;
    ret = MHD_YES;
  } else if (new_request && *upload_data_size == 0 && request->data) {
    // Data hasn't arrived yet
    ret = MHD_YES;
  } else if (*upload_data_size == 0) {    
    if (request->data) buffer_in(request->data, "\0", 1);
    HTTPResponse response;
    memset(&response, 0, sizeof(HTTPResponse));
    response.free_content = false;    
    handle_request(httpd, request, &response);

    // TODO This whole response handling bit is pretty messy, maybe it would be better to have some functions
    // that build the MHD_Response object directly and put it in the HTTPResponse.
    struct MHD_Response *mhd_response = MHD_create_response_from_data(strlen(response.content), response.content, response.free_content, MHD_NO);
    MHD_add_response_header(mhd_response, MHD_HTTP_HEADER_CONTENT_TYPE, response.content_type);

    if (response.location) {
      char buff[256];
      char *host = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, MHD_HTTP_HEADER_HOST);
      snprintf(buff, 256, "http://%s%s", host, response.location);
      MHD_add_response_header(mhd_response, MHD_HTTP_HEADER_LOCATION, buff);

    }
    ret = MHD_queue_response(connection, response.code, mhd_response);
    MHD_destroy_response(mhd_response);

    if (response.free_content) {
      if (response.location) free(response.location);
    }

    struct timeval end_time;
    gettimeofday(&end_time, NULL);
    info("%s %s %i %.7fs %i", method, raw_url, response.code, tdiff(request->start_time, end_time), strlen(response.content));

    free_buffer(request->data);
    free(request);
  }

  return ret;
}

static int access_policy(void *ip_vp, const struct sockaddr * addr, socklen_t addrlen) {
  char *allowed_ip = ip_vp;
  int allow = MHD_NO;

  if (allowed_ip) {
    if (addr->sa_family == AF_INET) {
      char ip[16];
      inet_ntop(AF_INET, &(((struct sockaddr_in *)addr)->sin_addr), ip, sizeof (ip));

      if (0 == strncmp(allowed_ip, ip, sizeof(ip))) {
        allow = MHD_YES;
      } else {
        info("Rejected connection from %s because it didn't match allowed ip %s", ip, allowed_ip);
      }
    }
  } else {
    allow = MHD_YES;
  }

  return allow;
}

#define COMPILE_REGEX(regex, pattern) \
  if ((regex_error = regcomp(regex, pattern, REG_EXTENDED | REG_NOSUB)) != 0) { \
    char buffer[1024]; \
    regerror(regex_error, &httpd->start_job_regex, buffer, sizeof(buffer)); \
    fatal("Error compiling REGEX: %s", buffer); \
  }

Httpd * httpd_start(HttpConfig *config, ClassificationEngine *ce, ItemCache *item_cache, TaggerCache * tagger_cache) {
  Httpd *httpd = malloc(sizeof(Httpd));
  if (httpd) {
    int regex_error;
    httpd->config = config;
    httpd->ce = ce;
    httpd->item_cache = item_cache;
    httpd->tagger_cache = tagger_cache;

    COMPILE_REGEX(&httpd->start_job_regex,                    "^/classifier/jobs/?(.xml)?$");
    COMPILE_REGEX(&httpd->job_status_regex,                   "^/classifier/jobs/.+(.xml)*$");
    COMPILE_REGEX(&httpd->about_regex,                        "^/classifier(.xml)?$");
    COMPILE_REGEX(&httpd->item_cache_create_feed_items_regex, "^/feed_items/?$");
    COMPILE_REGEX(&httpd->item_cache_feed_items_regex,        "^/feed_items/([0-9]+)$");
    COMPILE_REGEX(&httpd->get_clues_regex,                    "^/classifier/clues");

    httpd->mhd = MHD_start_daemon(MHD_USE_THREAD_PER_CONNECTION | MHD_USE_DEBUG,
                                  httpd->config->port,
                                  access_policy,
                                  (void*) httpd->config->allowed_ip,
                                  process_request,
                                  httpd,
                                  MHD_OPTION_END);
    if (NULL == httpd->mhd) {
      fatal("Could not start httpd: %s", strerror(errno));
    }
  }

  return httpd;
}

void httpd_stop(Httpd *httpd) {
  MHD_stop_daemon(httpd->mhd);
  httpd->mhd = NULL;

  regfree(&httpd->start_job_regex);
  regfree(&httpd->job_status_regex);
  regfree(&httpd->about_regex);
  regfree(&httpd->item_cache_create_feed_items_regex);
  regfree(&httpd->item_cache_feed_items_regex);
  regfree(&httpd->get_clues_regex);

  free(httpd);
}
