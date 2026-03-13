/*
Replay Buffer - move to folder
Copyright (C) 2024 Yelov

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include <window-utils.h>
#include <string-utils.h>
#include <common.h>

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <util/dstr.h>
#include <plugin-support.h>

#include <windows.h>
#include <psapi.h>
#include <process.h>

// Declarations
int move_file_to_new_location(const wchar_t *source_file_path,
			      const wchar_t *exe_file_path);
void on_frontend_event(enum obs_frontend_event event, void *data);

// 定义传递给线程的数据结构
typedef struct {
	wchar_t replay_path[MAX_PATH];
	wchar_t window_name[MAX_PATH];
} MoveTaskData;

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

// 异步移动和删除文件的后台线程
unsigned __stdcall MoveFileThread(void *param)
{
	MoveTaskData *data = (MoveTaskData *)param;

	// 1. 稍微等待，给 OBS 足够的时间来启动后台的自动封装 (Remux) 任务
	Sleep(3000);

	// 获取路径的各个部分
	wchar_t drive[_MAX_DRIVE];
	wchar_t dir[_MAX_DIR];
	wchar_t filename[_MAX_FNAME];
	wchar_t ext[_MAX_EXT];
	_wsplitpath_s(data->replay_path, drive, _MAX_DRIVE, dir, _MAX_DIR,
		      filename, _MAX_FNAME, ext, _MAX_EXT);

	// 构造潜在的 mp4 封装输出路径
	wchar_t mp4_path[MAX_PATH];
	swprintf(mp4_path, MAX_PATH, L"%ls%ls%ls.mp4", drive, dir, filename);

	// 2. 轮询检查文件是否正在被 OBS (FFmpeg) 读写
	int retries = 300;
	while (retries > 0) {
		bool is_locked = false;

		// 检查原文件 (mkv) 是否被占用
		HANDLE hMkv = CreateFileW(data->replay_path,
					  GENERIC_READ | GENERIC_WRITE, 0, NULL,
					  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
					  NULL);
		if (hMkv == INVALID_HANDLE_VALUE) {
			if (GetLastError() == ERROR_SHARING_VIOLATION) {
				is_locked = true;
			}
		} else {
			CloseHandle(hMkv);
		}

		// 检查目标文件 (mp4) 是否被占用
		HANDLE hMp4 = CreateFileW(mp4_path,
					  GENERIC_READ | GENERIC_WRITE, 0, NULL,
					  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
					  NULL);
		if (hMp4 == INVALID_HANDLE_VALUE) {
			if (GetLastError() == ERROR_SHARING_VIOLATION) {
				is_locked = true;
			}
		} else {
			CloseHandle(hMp4);
		}

		// 如果两个文件都没有被占用，说明封装已经完成
		if (!is_locked) {
			break;
		}

		Sleep(1000);
		retries--;
	}

	// 3. 移动 MP4 并删除 MKV
	bool mp4_exists =
		(GetFileAttributesW(mp4_path) != INVALID_FILE_ATTRIBUTES);
	bool is_original_mp4 = (wcsicmp(ext, L".mp4") == 0);

	if (!is_original_mp4 && mp4_exists) {
		// 如果生成了 mp4，尝试将其移动到分类文件夹
		if (move_file_to_new_location(mp4_path, data->window_name) ==
		    SUCCESS) {
			// 移动成功后，安全删除原始的 MKV 文件
			if (DeleteFileW(data->replay_path)) {
				obs_log(LOG_INFO,
					"Successfully deleted original file: %ls",
					data->replay_path);
			} else {
				obs_log(LOG_ERROR,
					"Failed to delete original file: %ls",
					data->replay_path);
			}
		} else {
			obs_log(LOG_ERROR,
				"Failed to move mp4 file, original mkv was kept: %ls",
				data->replay_path);
		}
	} else {
		// 备用方案：如果根本没有生成 mp4，或者原文件就是 mp4，直接移动原文件
		if (GetFileAttributesW(data->replay_path) !=
		    INVALID_FILE_ATTRIBUTES) {
			move_file_to_new_location(data->replay_path,
						  data->window_name);
		}
	}

	// 释放分配的内存
	free(data);
	return 0;
}

bool obs_module_load(void)
{
	obs_log(LOG_INFO, "plugin loaded successfully (version %s)",
		PLUGIN_VERSION);
	obs_frontend_add_event_callback(on_frontend_event, NULL);
	return TRUE;
}

void obs_module_unload(void)
{
	obs_log(LOG_INFO, "plugin unloaded");
	obs_frontend_remove_event_callback(on_frontend_event, NULL);
}

static void on_frontend_event(enum obs_frontend_event event, void *data)
{
	if (event != OBS_FRONTEND_EVENT_REPLAY_BUFFER_SAVED)
		return;

	// 获取回放路径
	char *replay_path = obs_frontend_get_last_replay();
	if (!replay_path) {
		obs_log(LOG_ERROR, "Failed to fetch the last replay");
		return;
	}

	// 获取活动窗口的名称
	wchar_t window_name[MAX_PATH];
	if (get_active_window_name(window_name, MAX_PATH) == FAILURE) {
		obs_log(LOG_ERROR, "Failed to get active window");
		bfree(replay_path);
		return;
	}

	// 为后台线程分配内存
	MoveTaskData *task_data = (MoveTaskData *)malloc(sizeof(MoveTaskData));
	if (!task_data) {
		obs_log(LOG_ERROR, "Failed to allocate memory for move task");
		bfree(replay_path);
		return;
	}

	swprintf(task_data->replay_path, MAX_PATH, L"%hs", replay_path);
	replace_char(task_data->replay_path, L'/', L'\\');
	wcsncpy_s(task_data->window_name, MAX_PATH, window_name, _TRUNCATE);

	// 创建后台线程
	HANDLE hThread = (HANDLE)_beginthreadex(NULL, 0, MoveFileThread,
						task_data, 0, NULL);
	if (hThread) {
		CloseHandle(hThread);
	} else {
		obs_log(LOG_ERROR, "Failed to create thread for moving files");
		free(task_data);
	}

	bfree(replay_path);
}

static int move_file_to_new_location(const wchar_t *source_file_path,
				     const wchar_t *window_name)
{
	// 1. Construct the new file path
	wchar_t drive[_MAX_DRIVE];
	wchar_t extension[_MAX_EXT];
	wchar_t filename[_MAX_FNAME];
	wchar_t dir[_MAX_DIR];
	_wsplitpath_s(source_file_path, drive, _MAX_DRIVE, dir, _MAX_DIR,
		      filename, _MAX_FNAME, extension, _MAX_EXT);

	wchar_t new_dir[MAX_PATH];
	swprintf(new_dir, MAX_PATH, L"%ls%ls%ls", drive, dir, window_name);

	wchar_t new_file_path[MAX_PATH];
	swprintf(new_file_path, MAX_PATH, L"%ls\\%ls%ls", new_dir, filename,
		 extension);

	// 2. Create the directory if it doesn't exist
	CreateDirectoryW(new_dir, NULL);

	// 3. Move recording
	obs_log(LOG_INFO, "Moving %ls to %ls", source_file_path, new_file_path);
	if (!MoveFileW(source_file_path, new_file_path))
		return FAILURE;

	return SUCCESS;
}
