# CodexAstraLocal: Testing belongs only to release.beta.alpha versions with alpha > 0.
# Native and Android UI share this compiled decision, independent of build type/flavor.
function(read_uberhar_alpha_version version_file result)
    set(is_alpha false)
    if(EXISTS "${version_file}")
        file(READ "${version_file}" version)
        string(STRIP "${version}" version)
        if(version MATCHES "^[0-9]+\\.[0-9]+\\.([0-9]+)$")
            # CodexAstraLocal: String matching avoids overflow and treats an all-zero alpha as OFF.
            if(CMAKE_MATCH_1 MATCHES "[1-9]")
                set(is_alpha true)
            endif()
        endif()
    endif()
    set(${result} ${is_alpha} PARENT_SCOPE)
endfunction()
