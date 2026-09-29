# Windows 7 TLS runtime provenance

- Scope: `libcrypto-1_1-x64.dll`, `libssl-1_1-x64.dll`, and `LICENSE` in this directory. The two DLLs are used only by the Qt 5.15.2 Windows 7–8.1 client branch for HTTPS and WSS.
- Version: OpenSSL 1.1.1w, reported by the vendor's `openssl.exe version -a` as VC-WIN64A, built 2023-09-27 with `_WIN32_WINNT=0x0502`.
- Binary source: Shining Light Productions, `https://slproweb.com/download/Win64OpenSSL_Light-1_1_1w.exe`; installer SHA-256 `82FC564EFC673BE043DAC12BAEFC5F4D3276A7E11B222D0F948A70C7367DD162`. Extracted without running the installer.
- Official source cross-check: `openssl-1.1.1w.tar.gz` from the OpenSSL project, SHA-256 `CF3098950CB4D853AD95C0841F1F9C6D3DC102DCCFCACD521D93925208B76AC8`. The license file is copied from this source archive.
- DLL SHA-256: `libcrypto-1_1-x64.dll` `13071DC72A97CF3C84FA754714C99F3B91F3AA784393994401BEC0A7869CDB51`; `libssl-1_1-x64.dll` `6BA837CE8BAFE2BAD595164F14BC3E85905E5C7FE88A0A0EFB99A2BA9F025F72`.
- License: OpenSSL License and original SSLeay license; full terms in `LICENSE`.
- Maintenance: OpenSSL 1.1.1 is upstream end of life. This branch must remain limited to the older Windows systems that cannot load Qt 6; replacing it with a maintained TLS backend requires a separate compatibility project.
