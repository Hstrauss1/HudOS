//Hudson Strauss
#include "ramfs.h"
#include "alloc.h"
#include "string.h"

static ramfs_file_t files[RAMFS_MAX_FILES];

void ramfs_init(void){
	memset(files, 0, sizeof(files));
}

int ramfs_create(const char *name){
	// check if already exists
	int existing = ramfs_find(name);
	if(existing >= 0)
		return existing;

	// find free slot
	for(int i = 0; i < RAMFS_MAX_FILES; i++){
		if(!files[i].used){
			files[i].used = 1;
			files[i].size = 0;
			// copy name
			int j = 0;
			while(name[j] && j < RAMFS_MAX_NAME - 1){
				files[i].name[j] = name[j];
				j++;
			}
			files[i].name[j] = '\0';
			// allocate data buffer
			files[i].data = (unsigned char *)kmalloc(RAMFS_MAX_SIZE);
			if(!files[i].data){
				files[i].used = 0;
				return -1;
			}
			memset(files[i].data, 0, RAMFS_MAX_SIZE);
			return i;
		}
	}
	return -1; // no slots
}

int ramfs_find(const char *name){
	for(int i = 0; i < RAMFS_MAX_FILES; i++){
		if(files[i].used && str_eq(files[i].name, name))
			return i;
	}
	return -1;
}

int ramfs_write(int fd, const void *buf, int len){
	if(fd < 0 || fd >= RAMFS_MAX_FILES || !files[fd].used)
		return -1;
	if(len > RAMFS_MAX_SIZE)
		len = RAMFS_MAX_SIZE;
	memcpy(files[fd].data, buf, len);
	files[fd].size = len;
	return len;
}

int ramfs_read(int fd, void *buf, int len){
	if(fd < 0 || fd >= RAMFS_MAX_FILES || !files[fd].used)
		return -1;
	if(len > files[fd].size)
		len = files[fd].size;
	memcpy(buf, files[fd].data, len);
	return len;
}

int ramfs_size(int fd){
	if(fd < 0 || fd >= RAMFS_MAX_FILES || !files[fd].used)
		return -1;
	return files[fd].size;
}

const char *ramfs_name(int fd){
	if(fd < 0 || fd >= RAMFS_MAX_FILES || !files[fd].used)
		return "";
	return files[fd].name;
}

int ramfs_count(void){
	int count = 0;
	for(int i = 0; i < RAMFS_MAX_FILES; i++){
		if(files[i].used) count++;
	}
	return count;
}
