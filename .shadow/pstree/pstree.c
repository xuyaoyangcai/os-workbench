#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// 功能：列出指定目录中的文件和子目录
void list_directory(const char *path) {
    DIR *dir = opendir(path);  // 打开目录
    struct dirent *entry;  // 用于读取目录项

    if (dir == NULL) {  // 检查是否成功打开目录
        perror("opendir");
        return;
    }

    // 遍历目录中的每一项
    while ((entry = readdir(dir)) != NULL) {
        struct stat statbuf;  // 用于存储文件的状态信息
        char fullpath[1024];  // 用于存储文件的完整路径

        // 构造文件的完整路径
        snprintf(fullpath, sizeof(fullpath), "%s/%s", path, entry->d_name);

        // 使用 stat() 获取文件的状态信息
        if (stat(fullpath, &statbuf) == 0) {
            // 判断文件类型是否为目录
            if (S_ISDIR(statbuf.st_mode)) {
                // 判断是否为当前目录（.）或上级目录（..），避免递归遍历
                if (entry->d_name[0] != '.') {
                    printf("%s (directory)\n", entry->d_name);
                }
            } else {
                printf("%s (file)\n", entry->d_name);
            }
        } else {
            // 如果 stat() 失败，打印错误信息
            perror("stat");
        }
    }

    closedir(dir);  // 关闭目录
}

int main() {
    const char *directory_path = ".";  // 当前目录
    list_directory(directory_path);  // 列出当前目录下的文件和子目录
    return 0;
}
