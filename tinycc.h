#ifndef TINYCC_H
#define TINYCC_H

int tinycc_compile(const char *src_path, const char *out_path);
int tinycc_is_program(const char *path);
int tinycc_exec(const char *path, const char *task_name);
void tinycc_task_cleanup(int task_id);

#endif
