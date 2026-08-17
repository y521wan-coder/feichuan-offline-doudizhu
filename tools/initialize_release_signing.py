import argparse
import ctypes
import getpass
import hashlib
import secrets
import shutil
from ctypes import wintypes
from datetime import datetime
from pathlib import Path

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import rsa

CRYPTPROTECT_UI_FORBIDDEN = 0x1


class DataBlob(ctypes.Structure):
    _fields_ = [("cbData", wintypes.DWORD), ("pbData", ctypes.POINTER(ctypes.c_ubyte))]


def protect_for_current_user(data: bytes) -> bytes:
    source_buffer = ctypes.create_string_buffer(data)
    source = DataBlob(len(data), ctypes.cast(source_buffer, ctypes.POINTER(ctypes.c_ubyte)))
    output = DataBlob()
    crypt32 = ctypes.windll.crypt32
    kernel32 = ctypes.windll.kernel32
    if not crypt32.CryptProtectData(ctypes.byref(source), "FPDZ release signing", None, None, None, CRYPTPROTECT_UI_FORBIDDEN, ctypes.byref(output)):
        raise ctypes.WinError()
    try:
        return ctypes.string_at(output.pbData, output.cbData)
    finally:
        kernel32.LocalFree(output.pbData)


def archive_if_present(path: Path):
    if not path.exists():
        return
    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    archived = path.with_name(f"{path.stem}.unpublished-old-{stamp}{path.suffix}")
    counter = 1
    while archived.exists():
        archived = path.with_name(f"{path.stem}.unpublished-old-{stamp}-{counter}{path.suffix}")
        counter += 1
    path.replace(archived)
    print(f"ARCHIVED={archived}")


def main():
    parser = argparse.ArgumentParser(description="生成无需重复输入密码的 DPAPI 保护发布密钥")
    parser.add_argument("--replace-unpublished", action="store_true")
    parser.add_argument("--prompt-password", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    primary = Path.home() / "Documents" / "飞船单机斗地主发布密钥"
    backup = Path("D:/飞船单机斗地主发布密钥备份")
    primary_private = primary / "release-signing-private.pem"
    backup_private = backup / "release-signing-private.pem"
    primary_password = primary / "release-signing-password.dpapi"
    backup_password = backup / "release-signing-password.dpapi"
    public_path = root / "assets" / "update" / "release-signing-public.pem"
    key_id_path = root / "assets" / "update" / "release-signing-key-id.txt"

    existing = [path for path in (primary_private, backup_private, primary_password, backup_password) if path.exists()]
    if existing and not args.replace_unpublished:
        raise SystemExit("发布密钥已经存在；只有尚未发布时才可使用 --replace-unpublished 轮换。")
    primary.mkdir(parents=True, exist_ok=True)
    backup.mkdir(parents=True, exist_ok=True)
    public_path.parent.mkdir(parents=True, exist_ok=True)
    if args.replace_unpublished:
        for path in existing:
            archive_if_present(path)

    private_key = rsa.generate_private_key(public_exponent=65537, key_size=3072)
    if args.prompt_password:
        password = None
        for attempt in range(1, 4):
            first = getpass.getpass(f"请输入新的发布私钥密码（第 {attempt}/3 次，至少 12 个字符）：")
            second = getpass.getpass("请再次输入相同的新密码：")
            if len(first) < 12:
                print("密码不足 12 个字符，请重新设置。")
                continue
            if first != second:
                print("两次密码不一致，请重新设置。")
                continue
            password = first.encode("utf-8")
            break
        if password is None:
            raise SystemExit("连续 3 次未能设置有效的新密码，密钥未更改。")
    else:
        password = secrets.token_bytes(32)
    encrypted_private = private_key.private_bytes(
        serialization.Encoding.PEM,
        serialization.PrivateFormat.PKCS8,
        serialization.BestAvailableEncryption(password),
    )
    public_key = private_key.public_key()
    public_pem = public_key.public_bytes(
        serialization.Encoding.PEM,
        serialization.PublicFormat.SubjectPublicKeyInfo,
    )
    public_der = public_key.public_bytes(
        serialization.Encoding.DER,
        serialization.PublicFormat.SubjectPublicKeyInfo,
    )
    protected_password = protect_for_current_user(password)

    primary_private.write_bytes(encrypted_private)
    backup_private.write_bytes(encrypted_private)
    primary_password.write_bytes(protected_password)
    backup_password.write_bytes(protected_password)
    public_path.write_bytes(public_pem)
    key_id = hashlib.sha256(public_der).hexdigest()
    key_id_path.write_text(key_id + "\n", encoding="utf-8")

    loaded = serialization.load_pem_private_key(primary_private.read_bytes(), password=password)
    if loaded.public_key().public_numbers() != public_key.public_numbers():
        raise SystemExit("新密钥自检失败。")
    if hashlib.sha256(primary_private.read_bytes()).digest() != hashlib.sha256(backup_private.read_bytes()).digest():
        raise SystemExit("主私钥和 D 盘备份不一致。")
    if primary_password.read_bytes() != backup_password.read_bytes():
        raise SystemExit("主 DPAPI 凭据和 D 盘备份不一致。")

    print(f"PRIMARY_PRIVATE={primary_private}")
    print(f"PRIMARY_DPAPI={primary_password}")
    print(f"BACKUP_PRIVATE={backup_private}")
    print(f"BACKUP_DPAPI={backup_password}")
    print(f"PUBLIC_KEY={public_path}")
    print(f"SIGNING_KEY_ID={key_id}")
    print("PASSWORD_INPUT_REQUIRED=NO")


if __name__ == "__main__":
    main()
