/* Copyright (c) 2007 The Kaphan Foundation
 *
 * Possession of a copy of this file grants no permission or license
 * to use, modify, or create derivate works.
 *
 * Please contact info@peerworks.org for further information.
 */

#include <pthread.h>
#include <config.h>
#include <stdlib.h>
#include <getopt.h>
#include <stdio.h>
#include <signal.h>
#include "logging.h"
#include "cls_config.h"
#include "classification_engine.h"
#include "httpd.h"
#include "misc.h"

#define DEFAULT_CONFIG_FILE "config/classifier.conf"
#define DEFAULT_LOG_FILE "log/classifier.log"
#define SHORT_OPTS "hvdc:l:"
#define USAGE "Usage: classifier [-dvh] [-c CONFIGFILE] [-l LOGFILE]\n"

static Config *config;
static ClassificationEngine *engine;
static Httpd *httpd;

volatile sig_atomic_t termination_in_progress = 0;

void termination_handler(int sig) {
  if (termination_in_progress) {
    raise(sig);
  } else {
    termination_in_progress = 1;
    fprintf(stderr, "Terminating classifier...\n");
    
    if (httpd) {
      fprintf(stderr, "\tShutting down httpd.\n");
      httpd_stop(httpd);
    }
    
    if (engine) {
      fprintf(stderr, "\tShutting down engine.\n");
      ce_stop(engine);
      free_classification_engine(engine);
    }
    
    fprintf(stderr, "Complete.\n");
    if (config) {
      free_config(config);
    }
    
    exit(sig);
  }
}

int main(int argc, char **argv) {
  int pid, sid;
  int daemonize = false;
  char *config_file = DEFAULT_CONFIG_FILE;
  char *log_file = DEFAULT_LOG_FILE;
  char real_config_file[MAXPATHLEN];
  char real_log_file[MAXPATHLEN];
  
  int longindex;
  int opt;
  static struct option long_options[] = {
      {"version", no_argument, 0, 'v'},
      {"help", no_argument, 0, 'h'},
      {"config-file", required_argument, 0, 'c'},
      {"log-file", required_argument, 0, 'l'},
      {0, 0, 0, }
  };
  
  while (-1 != (opt = getopt_long(argc, argv, SHORT_OPTS, long_options, &longindex))) {
    switch (opt) {
      case 'v':
        printf("%s\n", PACKAGE_STRING);
        exit(EXIT_SUCCESS);
      break;
      case 'c':
        config_file = optarg;
      break;
      case 'l':
        log_file = optarg;
      break;
      case 'd':
        daemonize = true;
      break;
      case 'h':
        // TODO Add help
        printf(USAGE);
        exit(0);
      default:
        exit(EXIT_FAILURE);
      break;
    }
  }
  
  if (NULL == realpath(config_file, real_config_file)) {
    fprintf(stderr, "Could not find %s\n", real_config_file);
    exit(EXIT_FAILURE);
  }
  
  if (NULL == realpath(log_file, real_log_file)) {
    fprintf(stderr, "Could not find %s\n", real_log_file);
    exit(EXIT_FAILURE);
  }
  
  if (daemonize) {
    pid = fork();
    
    if (pid < 0) {
      exit(EXIT_FAILURE);
    } else if (pid > 0) {
      // exit the foreground process
      exit(EXIT_SUCCESS);
    }
    
    umask(0);
    
    sid = setsid();
    if (sid < 0) {
      exit(EXIT_FAILURE);
    }
    
    if (chdir("/") < 0) {
      exit(EXIT_FAILURE);
    }
  }
  
  if (signal(SIGINT, termination_handler) == SIG_IGN) 
    signal(SIGINT, SIG_IGN);
  if (signal(SIGHUP, termination_handler) == SIG_IGN) 
    signal(SIGHUP, SIG_IGN);
  if (signal(SIGTERM, termination_handler) == SIG_IGN)
    signal(SIGTERM, SIG_IGN);
    
  initialize_logging(real_log_file);
  config = load_config(real_config_file);
  engine = create_classification_engine(config);
  httpd = httpd_start(config, engine);  
  ce_run(engine);
  
  return EXIT_SUCCESS;
}