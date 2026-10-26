# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""components/muse/sagwa_notify.c: which cat sound each message gets --
a chat mrrow in a conversation, an insistent meow for something important,
a chirp for anything else."""

from __future__ import annotations

import os
import shlex
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
TESTS = ROOT / "tests"
MUSE = ROOT / "components" / "muse"

MIN = 60.0
# (answers the user's turn, seconds since the last exchange or None, text, kind)
CASES = [
    (True, None, "Claro, aquí tienes la receta.", "chat"),
    (True, 5, "URGENTE: nada, era broma", "chat"),
    (False, 3 * MIN, "¿y luego qué pasó?", "chat"),
    (False, 3 * MIN, "Recordatorio: reunión a las 5", "chat"),
    (False, 3 * MIN, "URGENTE: llama a mamá", "urgent"),
    (False, None, "URGENTE: llama a mamá", "urgent"),
    (False, None, "Tienes una emergencia en casa", "urgent"),
    (False, 30 * MIN, "Recordatorio: reunión a las 5", "urgent"),
    (False, None, "Atención: el servidor está caído", "urgent"),
    (False, None, "The build FAILED on main", "urgent"),
    (False, None, "TU PAQUETE YA ESTA EN LA PUERTA", "urgent"),
    (False, None, "Llegó tu pedido!!", "urgent"),
    (False, None, "Tu pedido llegó", "notify"),
    (False, None, "¡Hola! Te dejé una foto del gato.", "notify"),
    (False, 11 * MIN, "Hay un nuevo episodio de tu podcast", "notify"),
    (False, None, "", "notify"),
    (False, None, "Se llama Sagwa y le gusta dormir", "notify"),
    (False, None, "NASA", "notify"),
]

HARNESS = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sagwa_notify.h"

int main(void)
{
    char line[1024];
    while (fgets(line, sizeof(line), stdin)) {
        line[strcspn(line, "\n")] = '\0';
        char *user = strtok(line, "|"), *age = strtok(NULL, "|"), *text = strtok(NULL, "");
        int64_t now = 100LL * 60 * 1000000;
        int64_t last = atof(age) < 0 ? 0 : now - (int64_t)(atof(age) * 1e6);
        printf("%s\n", sagwa_notify_kind_name(sagwa_notify_classify(user[0] == '1', text ? text : "", now, last)));
    }
    return 0;
}
"""


class SagwaNotifyTest(unittest.TestCase):
    def test_kinds(self) -> None:
        cc = shlex.split(os.environ.get("CC", "cc"))
        with tempfile.TemporaryDirectory() as tmp:
            harness = Path(tmp) / "harness.c"
            harness.write_text(HARNESS)
            binary = Path(tmp) / "sagwa_notify"
            built = subprocess.run(
                [*cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-g", "-I", str(MUSE),
                 str(harness), str(MUSE / "sagwa_notify.c"), "-o", str(binary)],
                capture_output=True, text=True,
            )
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            lines = "".join("%d|%s|%s\n" % (u, -1 if a is None else a, t) for u, a, t, _ in CASES)
            ran = subprocess.run([str(binary)], input=lines, capture_output=True, text=True, timeout=30)
            self.assertEqual(ran.returncode, 0, ran.stderr)
            got = ran.stdout.split("\n")
            for (u, a, t, want), kind in zip(CASES, got):
                self.assertEqual(kind, want, "%r (user_turn=%s, age=%s)" % (t, u, a))


if __name__ == "__main__":
    unittest.main()
