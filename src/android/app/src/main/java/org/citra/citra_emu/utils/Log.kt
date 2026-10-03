// Copyright 2023 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.utils

object Log {
    // AstraEH: Export selection is explicit; remove the never-updated old/current flag.
    // Flush on an IO worker before reading the current log, including shutdown totals.
    external fun flush(): Boolean

    // AstraEH: Called by the session worker; no logging or file writes in this query.
    external fun sessionFileHealthy(): Boolean

    external fun debug(message: String)

    external fun warning(message: String)

    external fun info(message: String)

    external fun error(message: String)

    external fun critical(message: String)
}
