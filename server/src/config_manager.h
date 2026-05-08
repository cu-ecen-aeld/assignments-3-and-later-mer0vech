#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

typedef struct {
  char port[10];
  int max_threads;
  int queue_size;
  char log_file[256];
} Config;

extern Config server_cfg;

// Loads configuration from a config file
void load_config(const char *filename);

#endif
