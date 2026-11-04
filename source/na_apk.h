/*
 * Copyright (C) 2026 Vita port contributors
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/**
 * @file  na_apk.h
 * @brief Reading files from base.apk.
 */

#ifndef NA_APK_H
#define NA_APK_H

#include <stddef.h>

/** Whether the APK contains this path (e.g. "assets/bin/Data/boot.config"). */
int na_apk_exists(const char *name);

/** Reads a file from the APK into a malloc'd, NUL-terminated buffer, or NULL. */
void *na_apk_read(const char *name, size_t *size);

#endif // NA_APK_H
