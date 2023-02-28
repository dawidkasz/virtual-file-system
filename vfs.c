#include <unistd.h>
#include <malloc.h>
#include <stdlib.h>
#include <memory.h>

#define MAGIC_NUMBER 0x754c7295
#define MAX_FILENAME_LEN 16
#define BLOCK_SIZE 4096
#define BLOCKS_PER_INODE 32
#define MAX_FILE_SIZE BLOCKS_PER_INODE * BLOCK_SIZE


typedef struct {
    u_int32_t magic_number;
    u_int32_t num_blocks;
} superblock_t;

typedef struct {
    char filename[MAX_FILENAME_LEN];
    u_int32_t filesize;
    u_int32_t blocks[BLOCKS_PER_INODE];
} inode_t;


FILE* fp = NULL;
superblock_t* superblock = NULL;
u_int8_t* blocks_bitmap = NULL;
inode_t* inodes = NULL;


static inline u_int32_t calc_bitmap_size(u_int32_t num_blocks) {
    return (num_blocks / 8) + 1;
}

u_int32_t get_block_address(u_int32_t block_index) {
    u_int32_t bitmap_size = calc_bitmap_size(superblock->num_blocks);
    u_int32_t inodes_size = superblock->num_blocks * sizeof(inode_t);
    return sizeof(superblock_t) + bitmap_size + inodes_size + block_index * BLOCK_SIZE;
}

u_int8_t is_block_empty(u_int32_t index) {
    u_int32_t node = index / 8;
    u_int32_t pos = index % 8;
    u_int8_t bitmap_node = blocks_bitmap[node];

    return ((bitmap_node & 1<<(7 - pos)) == 0)? 1 : 0;
}

void set_block_used(u_int32_t index) {
    u_int32_t node = index / 8;
    u_int32_t pos = index % 8;

    blocks_bitmap[node] |= 1<<(7 - pos);
}

void set_block_unused(u_int32_t index) {
    u_int32_t node = index / 8;
    u_int32_t pos = index % 8;

    blocks_bitmap[node] &= ~(1<<(7 - pos));
}

void clear_variables() {
    free(superblock);
    free(blocks_bitmap);
    free(inodes);
    fclose(fp);
}

void create_new_vfs(char* vfs_filename, u_int32_t disk_size) {
    fp = fopen(vfs_filename, "wb");
    if(!fp) {
        perror("Can't create new vfs");
        exit(EXIT_FAILURE);
    }

    u_int32_t num_blocks = disk_size / BLOCK_SIZE;
    u_int32_t bitmap_size = calc_bitmap_size(num_blocks);
    u_int32_t inodes_size = num_blocks * sizeof(inode_t);

    u_int32_t vfs_size = sizeof(superblock_t) + bitmap_size + inodes_size + num_blocks * BLOCK_SIZE;

    ftruncate(fileno(fp), vfs_size);

    superblock = (superblock_t*)malloc(sizeof(superblock_t));
    superblock->magic_number = MAGIC_NUMBER;
    superblock->num_blocks = num_blocks;
    fwrite(superblock, sizeof(superblock_t), 1, fp);

    blocks_bitmap = malloc(bitmap_size);
    memset(blocks_bitmap, 0, bitmap_size);
    fwrite(blocks_bitmap, bitmap_size, 1, fp);

    inode_t* empty_inode = (inode_t*)malloc(sizeof(inode_t));
    strcpy(empty_inode->filename, "");
    empty_inode->filesize = 0;
    fwrite(empty_inode, sizeof(inode_t), num_blocks, fp);
    free(empty_inode);
}

void open_vfs(char* vfs_filename) {
    fp = fopen(vfs_filename, "r+");
    if(!fp) {
        perror("Can't open specified vfs file");
        exit(EXIT_FAILURE);
    }

    superblock = (superblock_t*)malloc(sizeof(superblock_t));
    fread(superblock, sizeof(superblock_t), 1, fp);

    if(superblock->magic_number != MAGIC_NUMBER) {
        fprintf(stderr, "Invalid file system, %d != %d", superblock->magic_number, MAGIC_NUMBER);
        clear_variables();
        exit(EXIT_FAILURE);
    }

    blocks_bitmap = (u_int8_t*)malloc(calc_bitmap_size(superblock->num_blocks));
    fread(blocks_bitmap, sizeof(u_int8_t), calc_bitmap_size(superblock->num_blocks), fp);

    inodes = (inode_t*)malloc(sizeof(inode_t) * superblock->num_blocks);
    fread(inodes, sizeof(inode_t), superblock->num_blocks, fp);
}

void copy_file_to(char* src_filepath, char* dst_filename) {
    u_int32_t filename_len = strlen(dst_filename);
    if(filename_len > MAX_FILENAME_LEN || filename_len == 0) {
        fprintf(stderr, "Invalid filename, length must be between 1 and : %d chars", MAX_FILENAME_LEN);
        clear_variables();
        exit(EXIT_FAILURE);
    }

    FILE* fp_src = fopen(src_filepath, "r");
    if(!fp_src) {
        perror("Can't copy specified file");
        clear_variables();
        exit(EXIT_FAILURE);
    }

    fseek(fp_src, 0, SEEK_END);
    u_int32_t filesize = ftell(fp_src);
    if(filesize > MAX_FILE_SIZE) {
        fprintf(stderr, "File is too big, max size: %d bytes", MAX_FILE_SIZE);
        clear_variables();
        fclose(fp_src);
        exit(EXIT_FAILURE);
    }
    fseek(fp_src, 0, SEEK_SET);

    inode_t* first_empty_inode = NULL;
    u_int32_t first_empty_inode_idx = -1;

    for(int i=0; i<superblock->num_blocks; ++i) {
        if(strcmp((&inodes[i])->filename, dst_filename) == 0) {
            fprintf(stderr, "File with such name already exists");
            clear_variables();
            fclose(fp_src);
            exit(EXIT_FAILURE);
        }

        if(first_empty_inode == NULL && strcmp((&inodes[i])->filename, "") == 0) {
            first_empty_inode = &inodes[i];
            first_empty_inode_idx = i;
        }
    }

    u_int32_t bitmap_size = calc_bitmap_size(superblock->num_blocks);
    u_int32_t first_empty_inode_pos = sizeof(superblock_t) + bitmap_size + sizeof(inode_t) * first_empty_inode_idx;

    strcpy(first_empty_inode->filename, dst_filename);
    first_empty_inode->filesize = filesize;

    u_int8_t buffer[BLOCK_SIZE];
    u_int32_t inode_block_idx = 0, block_idx = 0;

    while(1) {
        u_int32_t bytes_read = fread(buffer, sizeof(u_int8_t), BLOCK_SIZE, fp_src);
        if(bytes_read <= 0)
            break;

        while(block_idx < superblock->num_blocks && !is_block_empty(block_idx)) {
            ++block_idx;
        }

        if(!is_block_empty(block_idx)) {
            fprintf(stderr, "Not enough disk space");
            strcpy(first_empty_inode->filename, "");
            first_empty_inode->filesize = 0;
            fseek(fp,first_empty_inode_pos,SEEK_SET);
            fwrite(first_empty_inode, sizeof(inode_t), 1, fp);
            clear_variables();
            fclose(fp_src);
            exit(EXIT_FAILURE);
        }

        u_int32_t block_addr = get_block_address(block_idx);
        first_empty_inode->blocks[inode_block_idx++] = block_addr;

        set_block_used(block_idx);
        fseek(fp, block_addr, SEEK_SET);
        fwrite(buffer, bytes_read, 1, fp);
    }

    fseek(fp, first_empty_inode_pos, SEEK_SET);
    fwrite(first_empty_inode, sizeof(inode_t), 1, fp);

    fseek(fp, sizeof(superblock), SEEK_SET);
    fwrite(blocks_bitmap, calc_bitmap_size(superblock->num_blocks), 1, fp);

    fclose(fp_src);
}

void copy_file_from(char* src_filename, char* dst_filepath) {
    FILE* fp_dst = fopen(dst_filepath, "w");
    if(!fp_dst) {
        perror("Can't create specified file");
        clear_variables();
        exit(EXIT_FAILURE);
    }

    inode_t* file_inode = NULL;

    for(int i=0; i<superblock->num_blocks; ++i) {
        if(strcmp((&inodes[i])->filename, src_filename) == 0) {
            file_inode = &inodes[i];
            break;
        }
    }

    if(file_inode == NULL) {
        perror("Specified file doesn't exist");
        clear_variables();
        fclose(fp_dst);
        exit(EXIT_FAILURE);
    }

    u_int32_t inode_block_idx = 0, bytes_to_read_left = file_inode->filesize;
    u_int8_t buffer[BLOCK_SIZE];

    while(1) {
        u_int32_t block_addr = file_inode->blocks[inode_block_idx++];
        if(block_addr == 0)
            break;

        fseek(fp, block_addr, SEEK_SET);

        u_int32_t num_bytes_to_read = (bytes_to_read_left < BLOCK_SIZE)? bytes_to_read_left : BLOCK_SIZE;
        u_int32_t bytes_read = fread(buffer, sizeof(u_int8_t), num_bytes_to_read, fp);
        bytes_to_read_left -= bytes_read;

        fwrite(buffer, bytes_read, 1, fp_dst);
    }

    fclose(fp_dst);
}

void list_files() {
    printf("Name\t\tSize\n");
    for(int i=0; i<superblock->num_blocks; ++i) {
        char* filename = (&inodes[i])->filename;
        u_int32_t filesize = (&inodes[i])->filesize;

        if(strcmp(filename, "") != 0) {
            printf("%s\t\t%d\n", filename, filesize);
        }
    }
}

void remove_file(char* filename) {
    int32_t file_inode_idx = -1;
    inode_t* file_inode = NULL;
    for(int i=0; i<superblock->num_blocks; ++i) {
        if(strcmp((&inodes[i])->filename, filename) == 0) {
            file_inode = &inodes[i];
            file_inode_idx = i;
            break;
        }
    }

    if(file_inode == NULL) {
        fprintf(stderr, "File with given name doesn't exist.");
        clear_variables();
        exit(EXIT_FAILURE);
    }

    strcpy(file_inode->filename, "");
    file_inode->filesize = 0;

    u_int32_t bitmap_size = calc_bitmap_size(superblock->num_blocks);
    u_int32_t inodes_size = sizeof(inode_t) * superblock->num_blocks;

    for(int i=0; i<BLOCKS_PER_INODE; ++i) {
        if(file_inode->blocks[i] == 0) continue;
        u_int32_t block_idx = (file_inode->blocks[i] - sizeof(superblock) - bitmap_size - inodes_size) / BLOCK_SIZE;
        set_block_unused(block_idx);
        file_inode->blocks[i] = 0;
    }

    u_int32_t file_inode_addr = sizeof(superblock_t) + bitmap_size + sizeof(inode_t) * file_inode_idx;

    fseek(fp, file_inode_addr, SEEK_SET);
    fwrite(file_inode, sizeof(inode_t), 1, fp);

    fseek(fp, sizeof(superblock), SEEK_SET);
    fwrite(blocks_bitmap, bitmap_size, 1, fp);
}

void remove_vfs(char* vfs_filename) {
    int res = remove(vfs_filename);
    if(res != 0) {
        fprintf(stderr, "Can't remove vfs");
    }
}

void show_vfs_space_map() {
    u_int32_t address = 0;
    u_int32_t superblock_size = sizeof(superblock_t);
    u_int32_t bitmap_size = calc_bitmap_size(superblock->num_blocks);
    u_int32_t inodes_size = superblock->num_blocks * sizeof(inode_t);

    printf("Type\t\tAddress\t\tSize (B)\n");

    printf("Superblock\t%d\t\t%d\n", address, superblock_size);
    address += superblock_size;

    printf("Bitmap\t\t%d\t\t%d\n", address, bitmap_size);
    address += bitmap_size;

    printf("INodes\t\t%d\t\t%d\n", address, inodes_size);

    u_int8_t prev_space_type = is_block_empty(0);
    u_int32_t counter = 0;

    for(int i=0; i<superblock->num_blocks; ++i) {
        u_int8_t curr_space_type = is_block_empty(i);
        if(curr_space_type == prev_space_type) {
            ++counter;
        } else {
            char* type = (prev_space_type == 1)? "FREE" : "USED";
            printf("%s\t\t%d\t\t%d\n", type, get_block_address(i-counter), counter * BLOCK_SIZE);
            prev_space_type = curr_space_type;
            counter = 1;
        }
    }

    char* type = (prev_space_type == 1)? "FREE" : "USED";
    printf("%s\t\t%d\t\t%d\n", type, get_block_address(superblock->num_blocks-counter), counter * BLOCK_SIZE);
}

void help() {
    printf("Usage: ./vfs [action] [action_arguments]\n");
    printf("Actions:\n");
    printf("1) create [vfs_name] [disk_size] - create new virtual file system\n");
    printf("2) write [vfs_name] [src_filepath] [vfs_filename] - copy file from host to vfs\n");
    printf("3) read [vfs_name] [vfs_filename] [dst_filepath] - copy file from vfs to host\n");
    printf("4) ls [vfs_name] - list files on vfs\n");
    printf("5) rm [vfs_name] [filename] - remove file from vfs\n");
    printf("6) rm_vfs [vfs_name] - remove whole virtual file system\n");
    printf("7) map [vfs_name] - Show vfs space map and usage\n");
    printf("8) help - Show help\n");
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Invalid command\n");
        help();
        return 1;
    }

    char* action = argv[1];
    if(strcmp(action, "create") == 0) {
        if(argc != 4) {
            help();
            return 1;
        }

        create_new_vfs( argv[2], (u_int32_t)atoi(argv[3]));
        return 0;
    }

    if(strcmp(action, "help") == 0) {
        if(argc != 2) {
            help();
            return 1;
        }

        help();
        return 0;
    }

    if(argc < 3) {
        help();
        return 1;
    };

    open_vfs(argv[2]);

    if(strcmp(action, "write") == 0) {
        if(argc != 5) {
            help();
            clear_variables();
            return 1;
        }

        copy_file_to(argv[3], argv[4]);
    } else if(strcmp(action, "read") == 0) {
        if(argc != 5) {
            help();
            clear_variables();
            return 1;
        }

        copy_file_from(argv[3], argv[4]);
    } else if(strcmp(action, "ls") == 0) {
        if(argc != 3) {
            help();
            clear_variables();
            return 1;
        }

        list_files();
    } else if(strcmp(action, "rm") == 0) {
        if(argc != 4) {
            help();
            clear_variables();
            return 1;
        }

        remove_file(argv[3]);
    } else if(strcmp(action, "rm_vfs") == 0) {
        if(argc != 3) {
            help();
            clear_variables();
            return 1;
        }

        remove_vfs(argv[2]);
    } else if(strcmp(action, "map") == 0) {
        if(argc != 3) {
            help();
            clear_variables();
            return 1;
        }

        show_vfs_space_map();
    }

    clear_variables();
    return 0;
}