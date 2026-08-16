# CompilerWarnings.cmake - Project-wide warning configuration

function(fpdz_apply_warnings target)
    target_compile_options(${target} PRIVATE
        /W3
        /utf-8
        /permissive-
        /Zc:__cplusplus
        /EHsc
    )

    target_compile_definitions(${target} PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        $<$<CONFIG:Debug>:_DEBUG;FPDZ_DEBUG>
        $<$<CONFIG:Release>:NDEBUG>
    )
endfunction()
