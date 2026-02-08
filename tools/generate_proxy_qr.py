#!/usr/bin/env python3
"""
Генератор QR-кода для VLESS Reality подключения к серверу Kolibri.
Сканируй QR-код любым клиентом: v2rayNG, Nekoray, Hiddify, Streisand и т.д.
"""
from __future__ import annotations

import qrcode
from qrcode.image.styledpil import StyledPilImage
from qrcode.image.styles.colormasks import SolidFillColorMask
from PIL import Image, ImageDraw, ImageFont

# ── Параметры VLESS Reality подключения ──
VLESS_LINK = (
    "vless://ef532324-737c-4fb1-adc5-d48215bd033c"
    "@217.60.7.164:443"
    "?encryption=none"
    "&flow=xtls-rprx-vision"
    "&security=reality"
    "&sni=google.com"
    "&fp=chrome"
    "&pbk=qApnC7oIojwDfuTzY6w0LD-ivL3xZiYZfEob09WqxQ0"
    "&sid=a1b2c3d4e5f6"
    "&type=tcp"
    "&headerType=none"
    "#Kolibri-Reality"
)

OUTPUT_PATH = "/workspaces/kolibri-project/kolibri_proxy_qr.png"


def generate_qr() -> None:
    # Генерация QR
    qr = qrcode.QRCode(
        version=None,
        error_correction=qrcode.constants.ERROR_CORRECT_M,
        box_size=12,
        border=4,
    )
    qr.add_data(VLESS_LINK)
    qr.make(fit=True)

    # Стилизованное изображение
    qr_img = qr.make_image(
        image_factory=StyledPilImage,
        color_mask=SolidFillColorMask(
            back_color=(255, 255, 255),
            front_color=(30, 30, 30),
        ),
    ).convert("RGB")

    qr_w, qr_h = qr_img.size

    # Добавляем подпись снизу
    padding_top = 30
    padding_bottom = 80
    canvas_w = qr_w
    canvas_h = qr_h + padding_top + padding_bottom

    canvas = Image.new("RGB", (canvas_w, canvas_h), (255, 255, 255))
    canvas.paste(qr_img, (0, padding_top))

    draw = ImageDraw.Draw(canvas)

    # Заголовок сверху
    title = "🦜 Kolibri Reality Proxy"
    try:
        font_title = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 22)
        font_sub = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 14)
    except OSError:
        font_title = ImageFont.load_default()
        font_sub = ImageFont.load_default()

    # Заголовок
    bbox = draw.textbbox((0, 0), title, font=font_title)
    tw = bbox[2] - bbox[0]
    draw.text(((canvas_w - tw) // 2, 4), title, fill=(50, 50, 50), font=font_title)

    # Подпись
    subtitle = "VLESS + XTLS-Reality  |  217.60.7.164:443  |  SNI: google.com"
    bbox2 = draw.textbbox((0, 0), subtitle, font=font_sub)
    sw = bbox2[2] - bbox2[0]
    draw.text(
        ((canvas_w - sw) // 2, qr_h + padding_top + 10),
        subtitle,
        fill=(100, 100, 100),
        font=font_sub,
    )

    hint = "Сканируй в v2rayNG / Hiddify / Nekoray / Streisand"
    bbox3 = draw.textbbox((0, 0), hint, font=font_sub)
    hw = bbox3[2] - bbox3[0]
    draw.text(
        ((canvas_w - hw) // 2, qr_h + padding_top + 35),
        hint,
        fill=(150, 150, 150),
        font=font_sub,
    )

    canvas.save(OUTPUT_PATH, "PNG")
    print(f"✅ QR-код сохранён: {OUTPUT_PATH}")
    print(f"📏 Размер: {canvas_w}x{canvas_h} px")
    print(f"\n📋 Ссылка для подключения:")
    print(VLESS_LINK)


if __name__ == "__main__":
    generate_qr()
