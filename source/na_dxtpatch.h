/*
 * Copyright (C) 2026 Vita port contributors
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#ifndef NA_DXTPATCH_H
#define NA_DXTPATCH_H

/** Turns the ETC2 textures in the extracted asset files into DXT5, once. */
void na_dxt_patch(void);

/** An asset file was extracted again: patch it on the next start. */
void na_dxt_patch_invalidate(void);

#endif
