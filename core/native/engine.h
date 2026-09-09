#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void qdr_set_test_mode(int enabled);

int qdr_list_disks_json(wchar_t** json_out);
int qdr_diagnose_json(const wchar_t* source, int deep, wchar_t** json_out);
int qdr_tree_json(const wchar_t* source, int partition_index, const wchar_t* ntfs_path, wchar_t** json_out);
int qdr_image(const wchar_t* source, const wchar_t* dest, volatile int* stop_flag,
              void (*progress)(unsigned long long done, unsigned long long total, const wchar_t* msg, void* user),
              void* user, wchar_t** json_out);
int qdr_copy_out(const wchar_t* source, int partition_index, const wchar_t* ntfs_path,
                 const wchar_t* dest_dir, volatile int* stop_flag,
                 void (*progress)(unsigned long long done, unsigned long long total, const wchar_t* msg, void* user),
                 void* user, wchar_t** json_out);
int qdr_carve(const wchar_t* source, int partition_index, const wchar_t* dest_dir,
              volatile int* stop_flag,
              void (*progress)(unsigned long long done, unsigned long long total, const wchar_t* msg, void* user),
              void* user, wchar_t** json_out);
int qdr_repair_gpt(const wchar_t* source, int do_write, wchar_t** json_out);
int qdr_find_volume_json(const wchar_t* source, int partition_index, wchar_t** json_out);
int qdr_bitlocker_unlock(const wchar_t* volume,
                         const wchar_t* recovery_password,
                         const wchar_t* bek_path,
                         const wchar_t* passphrase,
                         wchar_t** json_out);
int qdr_bitlocker_attach_image(const wchar_t* image, wchar_t** json_out);
void qdr_free_json(wchar_t* p);

#ifdef __cplusplus
}
#endif
