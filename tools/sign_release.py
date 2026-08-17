import argparse
import base64
import ctypes
import getpass
import hashlib
import json
from pathlib import Path
from ctypes import wintypes

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import padding, rsa


CRYPTPROTECT_UI_FORBIDDEN = 0x1


class DataBlob(ctypes.Structure):
    _fields_ = [("cbData", wintypes.DWORD), ("pbData", ctypes.POINTER(ctypes.c_ubyte))]


def unprotect_for_current_user(data: bytes) -> bytes:
    source_buffer = ctypes.create_string_buffer(data)
    source = DataBlob(len(data), ctypes.cast(source_buffer, ctypes.POINTER(ctypes.c_ubyte)))
    output = DataBlob()
    description = ctypes.c_wchar_p()
    crypt32 = ctypes.windll.crypt32
    kernel32 = ctypes.windll.kernel32
    if not crypt32.CryptUnprotectData(ctypes.byref(source), ctypes.byref(description), None, None, None, CRYPTPROTECT_UI_FORBIDDEN, ctypes.byref(output)):
        raise ctypes.WinError()
    try:
        return ctypes.string_at(output.pbData, output.cbData)
    finally:
        if description:
            kernel32.LocalFree(description)
        kernel32.LocalFree(output.pbData)

PRODUCT_NAME = "飞船单机斗地主"
PRODUCT_KEY = "feichuan_offline_doudizhu"
PLATFORM = "windows"
CHANNEL = "stable"


def main():
    root = Path(__file__).resolve().parent.parent
    version = (root / "version.txt").read_text(encoding="utf-8-sig").strip()
    parser = argparse.ArgumentParser(description="生成飞船单机斗地主固定公钥发布清单")
    parser.add_argument("--installer", default=str(root / "releases" / f"{PRODUCT_NAME}-Setup-{version}.exe"))
    parser.add_argument("--private-key", default=str(Path.home() / "Documents" / "飞船单机斗地主发布密钥" / "release-signing-private.pem"))
    parser.add_argument("--public-key", default=str(root / "assets" / "update" / "release-signing-public.pem"))
    parser.add_argument("--password-blob", default=str(Path.home() / "Documents" / "飞船单机斗地主发布密钥" / "release-signing-password.dpapi"))
    parser.add_argument("--output", default=str(root / "releases" / f"{PRODUCT_NAME}-Setup-{version}.release-manifest.json"))
    args = parser.parse_args()

    installer = Path(args.installer).resolve()
    private_path = Path(args.private_key).resolve()
    public_path = Path(args.public_key).resolve()
    output = Path(args.output).resolve()
    expected_name = f"{PRODUCT_NAME}-Setup-{version}.exe"
    if installer.name != expected_name:
        raise SystemExit(f"安装包文件名必须是：{expected_name}")
    for path in (installer, private_path, public_path):
        if not path.is_file():
            raise SystemExit(f"缺少文件：{path}")
    if installer.read_bytes()[:2] != b"MZ":
        raise SystemExit("安装包不是有效的 Windows MZ 可执行文件。")

    encrypted_private_key = private_path.read_bytes()
    password_blob = Path(args.password_blob).resolve()
    if password_blob.is_file():
        try:
            password = unprotect_for_current_user(password_blob.read_bytes())
        except OSError as exc:
            raise SystemExit(f"Windows DPAPI 无法解密发布凭据：{exc}") from exc
        try:
            private_key = serialization.load_pem_private_key(encrypted_private_key, password=password)
        except ValueError as exc:
            raise SystemExit("DPAPI 凭据与发布私钥不匹配，未生成发布清单。") from exc
        print("PASSWORD_SOURCE=WINDOWS_DPAPI")
    else:
        private_key = None
        for attempt in range(1, 4):
            password = getpass.getpass(f"请输入发布私钥密码（第 {attempt}/3 次）：").encode("utf-8")
            try:
                private_key = serialization.load_pem_private_key(encrypted_private_key, password=password)
                break
            except ValueError:
                if attempt < 3:
                    print("密码不正确，请确认大小写、输入法和符号后重试。")
        if private_key is None:
            raise SystemExit("连续 3 次无法解密私钥，未生成发布清单，也未上传服务器。")
    public_key = serialization.load_pem_public_key(public_path.read_bytes())
    if not isinstance(private_key, rsa.RSAPrivateKey) or private_key.key_size != 3072:
        raise SystemExit("发布私钥必须是 3072 位 RSA 私钥。")
    if private_key.public_key().public_numbers() != public_key.public_numbers():
        raise SystemExit("私钥和仓库固定公钥不匹配。")

    file_size = installer.stat().st_size
    sha256 = hashlib.sha256(installer.read_bytes()).hexdigest()
    public_der = public_key.public_bytes(serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo)
    key_id = hashlib.sha256(public_der).hexdigest()
    tracked_key_id = (root / "assets" / "update" / "release-signing-key-id.txt").read_text(encoding="utf-8-sig").strip().lower()
    if key_id != tracked_key_id:
        raise SystemExit("公钥与仓库登记的签名密钥 ID 不一致。")

    payload = (
        "FPDZ-UPDATE-SIGNATURE-V1\n"
        f"product_key={PRODUCT_KEY}\n"
        f"platform={PLATFORM}\n"
        f"channel={CHANNEL}\n"
        f"version={version}\n"
        f"file_size={file_size}\n"
        f"sha256={sha256}\n"
    ).encode("utf-8")
    signature = private_key.sign(payload, padding.PKCS1v15(), hashes.SHA256())
    public_key.verify(signature, payload, padding.PKCS1v15(), hashes.SHA256())
    manifest = {
        "product_name": PRODUCT_NAME,
        "product_key": PRODUCT_KEY,
        "platform": PLATFORM,
        "channel": CHANNEL,
        "version": version,
        "release_notes": f"{PRODUCT_NAME} {version} Windows x64 版本",
        "package_filename": expected_name,
        "file_size": file_size,
        "sha256": sha256,
        "signature_algorithm": "rsa-pkcs1-sha256",
        "signature_payload_version": 1,
        "signing_key_id": key_id,
        "signature": base64.b64encode(signature).decode("ascii"),
        "public_key_pem": public_path.read_text(encoding="ascii"),
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"MANIFEST={output}")
    print(f"FILE_SIZE={file_size}")
    print(f"SHA256={sha256}")
    print(f"SIGNING_KEY_ID={key_id}")


if __name__ == "__main__":
    main()
