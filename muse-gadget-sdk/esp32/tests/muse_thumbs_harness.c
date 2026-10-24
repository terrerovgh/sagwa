/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* Decodes each PNG named on the command line to a thumbnail and prints
 * "<w> <h> <centre RGB565 hex>" or "error <why>", one line per file. */
#include <stdio.h>
#include <stdlib.h>

#include "muse_thumbs.h"

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        FILE *f = fopen(argv[i], "rb");
        if (!f) return 1;
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        rewind(f);
        unsigned char *b = malloc((size_t)n);
        if (!b || fread(b, 1, (size_t)n, f) != (size_t)n) return 1;
        fclose(f);
        int w = 0, h = 0;
        const char *why = NULL;
        uint16_t *t = muse_thumbs_decode_png(b, (size_t)n, &w, &h, &why);
        if (t) {
            printf("%d %d %04x\n", w, h, t[(h / 2) * w + w / 2]);
        } else {
            printf("error %s\n", why ? why : "?");
        }
        free(t);
        free(b);
    }
    return 0;
}
