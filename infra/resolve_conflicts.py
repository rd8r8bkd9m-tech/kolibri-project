#!/usr/bin/env python3
"""Автоматическое склеивание конфликтов git с базовыми эвристиками Kolibri."""

from __future__ import annotations

import argparse
import json
import logging
import sys
from fnmatch import fnmatch
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Sequence, Tuple
from scripts.policy_validate import zagruzit_blok

KONFLIKT_START = "<<<<<<<"
KONFLIKT_DELIM = "======="
KONFLIKT_END = ">>>>>>>"

LOGGER = logging.getLogger("resolve_conflicts")


def postroit_pravila(root: Path) -> List[Tuple[str, str]]:
    """Считывает AGENTS.md и возвращает список правил для путей."""

    agent = root / "AGENTS.md"
    if not agent.exists():
        return []

    try:
        blok = zagruzit_blok(agent)
    except SystemExit:
        return []

    sektsiya_files = _izvlech_files_sektsiyu(blok)
    if not sektsiya_files:
        return []

    pravila: List[Tuple[str, str]] = []
    for pattern in sektsiya_files.get("prefer_ours", []):
        pravila.append((pattern, "ours"))
    for pattern in sektsiya_files.get("prefer_theirs", []):
        pravila.append((pattern, "theirs"))
    return pravila


def _izvlech_files_sektsiyu(blok: str) -> Dict[str, List[str]]:
    """Извлекает из текстового блока раздел files и списки шаблонов."""

    sektsiya: Dict[str, List[str]] = {"prefer_ours": [], "prefer_theirs": []}
    tekushchaya_gruppa: Optional[str] = None
    v_files = False

    for syraja in blok.splitlines():
        stroka = syraja.rstrip()
        if not stroka:
            continue

        bez_otstupa = stroka.lstrip()
        otstup = len(stroka) - len(bez_otstupa)

        if not v_files:
            if bez_otstupa == "files:":
                v_files = True
            continue

        if otstup == 0 and not bez_otstupa.startswith("-") and bez_otstupa != "files:":
            break

        if otstup >= 2 and bez_otstupa.endswith(":"):
            tekushchaya_gruppa = bez_otstupa[:-1].strip()
            if tekushchaya_gruppa not in sektsiya:
                sektsiya[tekushchaya_gruppa] = []
            continue

        if tekushchaya_gruppa and bez_otstupa.startswith("-"):
            sektsiya.setdefault(tekushchaya_gruppa, []).append(bez_otstupa[1:].strip())

    return sektsiya


def vybrat_po_pravilam(
    file_path: Path, root: Path, pravila: Sequence[Tuple[str, str]]
) -> Optional[str]:
    """Возвращает предпочтительную стратегию для файла согласно правилам."""

    try:
        otnositelnyj = file_path.relative_to(root)
    except ValueError:
        otnositelnyj = file_path

    file_str = otnositelnyj.as_posix()
    for obrazec, strategiya in pravila:
        if fnmatch(file_str, obrazec):
            return strategiya
    return None


def _najti_marker(stroka: str) -> Tuple[int, Optional[str]]:
    """Ищет ближайший конфликтный маркер в строке."""

    luchshij_indeks = len(stroka)
    vybrannyj_marker: Optional[str] = None
    for marker in (KONFLIKT_START, KONFLIKT_DELIM, KONFLIKT_END):
        indeks = stroka.find(marker)
        if indeks != -1 and indeks < luchshij_indeks:
            luchshij_indeks = indeks
            vybrannyj_marker = marker
    if vybrannyj_marker is None:
        return -1, None
    return luchshij_indeks, vybrannyj_marker


def _propustit_marker_stroki(stroka: str, marker: str) -> str:
    """Удаляет маркер и сопутствующую служебную информацию, возвращая остаток строки."""

    ostatok = stroka[len(marker) :]
    if not ostatok:
        return ""
    ostatok = ostatok.lstrip()
    if marker in (KONFLIKT_START, KONFLIKT_END) and ostatok:
        chasti = ostatok.split(None, 1)
        if len(chasti) == 2:
            ostatok = chasti[1]
        else:
            ostatok = ""
    if ostatok.startswith("\n"):
        ostatok = ostatok[1:]
    return ostatok


def razobrat_konflikt(
    lines: Iterable[str],
    file_path: Path,
    root: Path,
    pravila: Sequence[Tuple[str, str]],
) -> Tuple[List[str], List[str]]:
    """Объединяет конфликтные блоки согласно правилам и возвращает применённые стратегии."""

    rezultat: List[str] = []
    ours: List[str] = []
    theirs: List[str] = []
    sostoyanie = "normal"
    strategii: List[str] = []
    nomer_konflikta = 0

    for stroka in lines:
        current = stroka
        while current:
            indeks, marker = _najti_marker(current)
            if indeks > 0:
                prefix = current[:indeks]
                if prefix:
                    if sostoyanie == "ours":
                        ours.append(prefix)
                    elif sostoyanie == "theirs":
                        theirs.append(prefix)
                    else:
                        rezultat.append(prefix)
                current = current[indeks:]
                continue
            if marker is None:
                if sostoyanie == "ours":
                    ours.append(current)
                elif sostoyanie == "theirs":
                    theirs.append(current)
                else:
                    rezultat.append(current)
                break
            if marker == KONFLIKT_START:
                sostoyanie = "ours"
                ours = []
                theirs = []
                nomer_konflikta += 1
                current = _propustit_marker_stroki(current, KONFLIKT_START)
                continue
            if marker == KONFLIKT_DELIM:
                sostoyanie = "theirs"
                current = _propustit_marker_stroki(current, KONFLIKT_DELIM)
                continue
            if marker == KONFLIKT_END:
                current = _propustit_marker_stroki(current, KONFLIKT_END)
                vybor = vybrat_po_pravilam(file_path, root, pravila) or "both"
                if vybor == "ours":
                    rezultat.extend(ours)
                elif vybor == "theirs":
                    rezultat.extend(theirs)
                else:
                    rezultat.extend(ours)
                    if theirs and ours and not ours[-1].endswith("\n"):
                        rezultat.append("\n")
                    rezultat.extend(theirs)
                strategii.append(vybor)
                LOGGER.info(
                    "Conflict in %s (block %d): selected %s",
                    file_path,
                    nomer_konflikta,
                    vybor,
                )
                sostoyanie = "normal"
                continue
    return rezultat, strategii


def obrabotat_fajl(
    path: Path, root: Path, pravila: Sequence[Tuple[str, str]]
) -> Dict[str, object]:
    """Читает файл, устраняет конфликтные маркеры и возвращает отчёт."""

    soderzhimoe = path.read_text(encoding="utf-8")
    if KONFLIKT_START not in soderzhimoe:
        return {"file": str(path), "status": "clean", "strategy": None}
    stroki = soderzhimoe.splitlines(keepends=True)
    novye, strategii = razobrat_konflikt(stroki, path, root, pravila)
    path.write_text("".join(novye), encoding="utf-8")
    strategiya = obobshit_strategiyu(strategii)
    return {"file": str(path), "status": "resolved", "strategy": strategiya}


def obobshit_strategiyu(strategii: Sequence[str]) -> str:
    """Определяет итоговую стратегию для файла."""

    unikalnye = set(strategii)
    if unikalnye == {"ours"}:
        return "ours"
    if unikalnye == {"theirs"}:
        return "theirs"
    return "both"


def nayti_fajly(root: Path) -> List[Path]:
    """Возвращает список отслеживаемых файлов с потенциальными конфликтами."""

    return [
        path
        for path in root.rglob("*")
        if path.is_file() and not path.name.startswith(".")
    ]


def postroit_otchet(root: Path) -> Dict[str, object]:
    """Формирует итоговый отчёт по всем обработанным файлам."""

    rezultaty: List[Dict[str, object]] = []
    pravila = postroit_pravila(root)
    for fajl in nayti_fajly(root):
        try:
            rezultaty.append(obrabotat_fajl(fajl, root, pravila))
        except UnicodeDecodeError:
            rezultaty.append({"file": str(fajl), "status": "skipped", "strategy": None})
    return {"files": rezultaty}


def main(argv: List[str]) -> int:
    parser = argparse.ArgumentParser(description="Автоконфликт Kolibri")
    parser.add_argument("--report", type=Path, default=None, help="путь для JSON-отчёта")
    args = parser.parse_args(argv)
    logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")
    koren = Path.cwd()
    otchet = postroit_otchet(koren)
    if args.report:
        args.report.write_text(json.dumps(otchet, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(otchet, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
