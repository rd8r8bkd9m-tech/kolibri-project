#!/usr/bin/env python3
import argparse
import hashlib
import json
import os
import pickle
import re
import shutil
import stat
import struct
import sys
import tempfile
import zlib

try:
    from kolibri_native import KolibriNative
    NATIVE_AVAILABLE = True
except (ImportError, FileNotFoundError):
    NATIVE_AVAILABLE = False

ARCHIVE_MARKER = "__kolibri_archive_version__"
ARCHIVE_VERSION = 2
FORMULA_DIGIT_MAGIC = b"KOLIBRI_FORMULA_DIGIT_CORPUS_V1\n"
FORMULA_DIGIT_MAGIC_V2 = b"KOLIBRI_FORMULA_DIGIT_CORPUS_V2\n"
FORMULA_DIGIT_BLOCK_SIZE = 1024 * 1024
ATOM_FORMULA_MAGIC = b"KOLIBRI_ATOM_FORMULA_V1\n"
PLACEHOLDER = b"\x00"
REPEATED_LINES_FORMULA_MAX_BYTES = 4 * 1024 * 1024
TOKEN_FORMULA_MAX_BYTES = 512 * 1024
AST_FORMULA_MAX_BYTES = 512 * 1024
BYTE_CHUNK_FORMULA_MAX_BYTES = 4 * 1024 * 1024

TOKEN_RE = re.compile(
    rb'"(?:\\.|[^"\\])*"'
    rb"|\'(?:\\.|[^\'\\])*\'"
    rb"|0[xX][0-9A-Fa-f]+"
    rb"|\d+(?:\.\d+)?(?:[eE][+-]?\d+)?"
    rb"|[A-Za-z_][A-Za-z0-9_]*"
    rb"|\s+"
    rb"|==|!=|<=|>=|->|\+\+|--|&&|\|\||<<|>>|\+=|-=|\*=|/=|%=|&=|\|=|\^=|::"
    rb"|.",
    re.DOTALL,
)
IDENT_RE = re.compile(rb"[A-Za-z_][A-Za-z0-9_]*\Z")
NUMBER_RE = re.compile(rb"(?:0[xX][0-9A-Fa-f]+|\d+(?:\.\d+)?(?:[eE][+-]?\d+)?)\Z")


class KolibriAGI:
    def __init__(self, storage=None):
        self.storage = os.path.expanduser(
            storage
            or os.environ.get("KOLIBRI_AGI_STORAGE", "")
            or "~/Library/Application Support/KolibriAGI"
        )
        self.atoms_dir = os.path.join(self.storage, "rules", "atoms")
        self.maps_dir = os.path.join(self.storage, "rules", "maps")
        self.seeds_dir = os.path.join(self.storage, "seeds")
        self.world_model_dir = os.path.join(self.storage, "world_model")
        for d in [self.atoms_dir, self.maps_dir, self.seeds_dir, self.world_model_dir]:
            os.makedirs(d, exist_ok=True)
        self.native = KolibriNative() if NATIVE_AVAILABLE else None

    def _quiet_native_call(self, fn, *args):
        if os.environ.get("KOLIBRI_AGI_VERBOSE_NATIVE"):
            return fn(*args)
        sys.stdout.flush()
        sys.stderr.flush()
        devnull = os.open(os.devnull, os.O_WRONLY)
        saved_out = os.dup(1)
        saved_err = os.dup(2)
        try:
            os.dup2(devnull, 1)
            os.dup2(devnull, 2)
            return fn(*args)
        finally:
            os.dup2(saved_out, 1)
            os.dup2(saved_err, 2)
            os.close(saved_out)
            os.close(saved_err)
            os.close(devnull)

    def _seed_name(self, seed_ref):
        name = os.path.basename(seed_ref)
        return name[:-5] if name.endswith(".seed") else name

    def _seed_path(self, seed_ref):
        if os.path.isfile(seed_ref):
            return seed_ref
        return os.path.join(self.seeds_dir, f"{self._seed_name(seed_ref)}.seed")

    def _read_seed(self, seed_ref):
        seed_path = self._seed_path(seed_ref)
        with open(seed_path, "rb") as f:
            seed_data = f.read()
        if len(seed_data) != 32:
            raise ValueError(f"Некорректный seed: {seed_path} ({len(seed_data)} bytes)")
        return seed_data, self._seed_name(seed_ref)

    def _load_archive(self, map_hash):
        map_path = os.path.join(self.maps_dir, map_hash)
        with open(map_path, "rb") as f:
            map_data = f.read()
        
        # Try formula decode for compressed map
        try:
            if map_data.startswith(FORMULA_DIGIT_MAGIC) or map_data.startswith(FORMULA_DIGIT_MAGIC_V2):
                corpus, info = self._formula_digit_decode(map_data)
                if isinstance(corpus, dict) and corpus.get("type") == "map_v2":
                    codec = corpus.get("codec", "kolibri.native" if self.native else "zlib")
                    if codec == "kolibri.native":
                        if not self.native:
                            raise RuntimeError("Карта требует kolibri.native, но библиотека недоступна")
                        raw_map, _ = self._quiet_native_call(self.native.decompress, corpus["data"])
                    elif codec == "zlib":
                        raw_map = zlib.decompress(corpus["data"])
                    else:
                        raise ValueError(f"Неизвестный codec карты: {codec}")
                    payload = pickle.loads(raw_map)
                else:
                    payload = corpus
            else:
                # Raw pickle or zlib map
                try:
                    payload = pickle.loads(zlib.decompress(map_data))
                except:
                    payload = pickle.loads(map_data)
        except Exception as e:
            # Final fallback
            payload = pickle.loads(map_data)

        return map_data, self._normalize_archive(payload)

    def _normalize_archive(self, payload):
        if isinstance(payload, dict) and payload.get(ARCHIVE_MARKER) == ARCHIVE_VERSION:
            payload.setdefault("files", {})
            payload.setdefault("dirs", {})
            payload.setdefault("symlinks", {})
            payload.setdefault("file_meta", {})
            return payload
        if isinstance(payload, dict):
            return {
                ARCHIVE_MARKER: 1,
                "files": payload,
                "dirs": {},
                "symlinks": {},
                "file_meta": {},
            }
        raise ValueError("Неизвестный формат карты проекта")

    def _metadata(self, path, follow_symlinks=True):
        st = os.stat(path) if follow_symlinks else os.lstat(path)
        return {
            "mode": stat.S_IMODE(st.st_mode),
            "mtime_ns": getattr(st, "st_mtime_ns", int(st.st_mtime * 1_000_000_000)),
        }

    def _snapshot(self, path):
        root_path = os.path.abspath(os.path.expanduser(path))
        files = {}
        dirs = set()
        symlinks = {}
        total_bytes = 0

        def add_file(abs_path, rel):
            nonlocal total_bytes
            with open(abs_path, "rb") as f:
                data = f.read()
            files[rel] = {
                "size": len(data),
                "sha256": hashlib.sha256(data).hexdigest(),
            }
            total_bytes += len(data)

        if os.path.islink(root_path):
            symlinks[os.path.basename(root_path)] = os.readlink(root_path)
        elif os.path.isfile(root_path):
            add_file(root_path, os.path.basename(root_path))
        elif os.path.isdir(root_path):
            for root, dirs_list, files_list in os.walk(root_path, topdown=True, followlinks=False):
                dirs_list.sort()
                files_list.sort()
                rel_root = os.path.relpath(root, root_path)
                if rel_root != ".":
                    dirs.add(rel_root)
                for d in list(dirs_list):
                    d_path = os.path.join(root, d)
                    rel = os.path.relpath(d_path, root_path)
                    if os.path.islink(d_path):
                        symlinks[rel] = os.readlink(d_path)
                        dirs_list.remove(d)
                for name in files_list:
                    f_path = os.path.join(root, name)
                    rel = os.path.relpath(f_path, root_path)
                    if os.path.islink(f_path):
                        symlinks[rel] = os.readlink(f_path)
                    elif os.path.isfile(f_path):
                        add_file(f_path, rel)
        else:
            raise FileNotFoundError(root_path)

        return {
            "files": files,
            "dirs": dirs,
            "symlinks": symlinks,
            "bytes": total_bytes,
        }

    def _compare_snapshots(self, original, restored):
        errors = []
        for rel, meta in original["files"].items():
            other = restored["files"].get(rel)
            if other != meta:
                errors.append(f"file mismatch: {rel}")
        for rel in sorted(set(restored["files"]) - set(original["files"])):
            errors.append(f"extra file: {rel}")
        for rel in sorted(original["dirs"] - restored["dirs"]):
            errors.append(f"missing dir: {rel}")
        for rel in sorted(restored["dirs"] - original["dirs"]):
            errors.append(f"extra dir: {rel}")
        for rel, target in original["symlinks"].items():
            if restored["symlinks"].get(rel) != target:
                errors.append(f"symlink mismatch: {rel}")
        for rel in sorted(set(restored["symlinks"]) - set(original["symlinks"])):
            errors.append(f"extra symlink: {rel}")
        return errors

    def _target_path(self, target_root, rel):
        norm = os.path.normpath(rel)
        if norm in (".", "", "..") or os.path.isabs(norm) or norm.startswith(".." + os.sep):
            raise ValueError(f"Опасный путь в архиве: {rel}")
        return os.path.join(target_root, norm)

    def _compress_residual(self, data):
        candidates = [("store", data)]

        try:
            level = 6 if len(data) > 16 * 1024 * 1024 else 9
            zdata = zlib.compress(data, level)
            if zlib.decompress(zdata) == data:
                candidates.append(("zlib", zdata))
        except Exception:
            pass

        native_max = int(os.environ.get("KOLIBRI_AGI_NATIVE_MAX_BYTES", str(8 * 1024 * 1024)))
        if self.native and len(data) <= native_max:
            try:
                ndata, _ = self._quiet_native_call(self.native.compress, data)
                restored, _ = self._quiet_native_call(self.native.decompress, ndata)
                if restored == data:
                    candidates.append(("kolibri.native", ndata))
            except Exception:
                pass

        return min(candidates, key=lambda item: len(item[1]))

    def _decompress_residual(self, codec, payload):
        if codec == "store":
            return payload
        if codec == "zlib":
            return zlib.decompress(payload)
        if codec == "kolibri.native":
            if not self.native:
                raise RuntimeError("Корпус требует kolibri.native, но библиотека недоступна")
            data, _ = self._quiet_native_call(self.native.decompress, payload)
            return data
        raise ValueError(f"Неизвестный residual codec: {codec}")

    def _is_probably_text(self, data):
        if not data:
            return True
        sample = data[:65536]
        if sample.count(0) > max(1, len(sample) // 200):
            return False
        printable = 0
        for b in sample:
            if b in (9, 10, 13) or 32 <= b <= 126 or b >= 128:
                printable += 1
        return printable / len(sample) >= 0.85

    def _env_int(self, name, default):
        raw = os.environ.get(name)
        if raw is None:
            return default
        try:
            value = int(raw)
        except ValueError:
            return default
        return max(0, value)

    def _pack_uvarints(self, values):
        out = bytearray()
        for value in values:
            value = int(value)
            while value >= 0x80:
                out.append((value & 0x7F) | 0x80)
                value >>= 7
            out.append(value)
        return bytes(out)

    def _unpack_uvarints(self, data, count):
        values = []
        value = 0
        shift = 0
        for b in data:
            value |= (b & 0x7F) << shift
            if b & 0x80:
                shift += 7
                if shift > 63:
                    raise ValueError("Поврежден varint stream")
                continue
            values.append(value)
            if len(values) == count:
                return values
            value = 0
            shift = 0
        if len(values) != count:
            raise ValueError("Поврежден varint stream: неверное количество индексов")
        return values

    def _encode_indices(self, indices):
        raw = self._pack_uvarints(indices)
        zdata = zlib.compress(raw, 9)
        if len(zdata) < len(raw):
            return "zlib-varint", zdata
        return "varint", raw

    def _decode_indices(self, codec, payload, count):
        if codec == "varint":
            raw = payload
        elif codec == "zlib-varint":
            raw = zlib.decompress(payload)
        else:
            raise ValueError(f"Неизвестный index codec: {codec}")
        return self._unpack_uvarints(raw, count)

    def _encode_blob(self, data):
        zdata = zlib.compress(data, 9)
        if len(zdata) < len(data):
            return "zlib", zdata
        return "store", data

    def _decode_blob(self, codec, payload):
        if codec == "store":
            return payload
        if codec == "zlib":
            return zlib.decompress(payload)
        raise ValueError(f"Неизвестный blob codec: {codec}")

    def _encode_chunk_table(self, chunks):
        lengths = [len(chunk) for chunk in chunks]
        length_codec, length_payload = self._encode_blob(self._pack_uvarints(lengths))
        data_codec, data_payload = self._encode_blob(b"".join(chunks))
        return {
            "count": len(chunks),
            "length_codec": length_codec,
            "lengths": length_payload,
            "data_codec": data_codec,
            "data": data_payload,
        }

    def _decode_chunk_table(self, formula, prefix, legacy_key=None):
        if legacy_key and legacy_key in formula:
            return formula[legacy_key]
        count = int(formula[f"{prefix}_count"])
        lengths = self._unpack_uvarints(
            self._decode_blob(formula[f"{prefix}_length_codec"], formula[f"{prefix}_lengths"]),
            count,
        )
        data = self._decode_blob(formula[f"{prefix}_data_codec"], formula[f"{prefix}_data"])
        chunks = []
        cursor = 0
        for length in lengths:
            chunk = data[cursor:cursor + length]
            if len(chunk) != length:
                raise ValueError(f"Повреждена таблица {prefix}")
            chunks.append(chunk)
            cursor += length
        if cursor != len(data):
            raise ValueError(f"Повреждена таблица {prefix}: лишние байты")
        return chunks

    def _periodic_pattern(self, data, max_pattern=256):
        n = len(data)
        if n < 8:
            return None
        limit = min(max_pattern, n // 2)
        for plen in range(1, limit + 1):
            pattern = data[:plen]
            if data[plen:plen * 2] != pattern[:min(plen, n - plen)]:
                continue
            ok = True
            for offset in range(plen, n, plen):
                chunk = data[offset:offset + plen]
                if chunk != pattern[:len(chunk)]:
                    ok = False
                    break
            if ok:
                full = n // plen
                tail = n % plen
                return pattern, full, data[n - tail:] if tail else b""
        return None

    def _dictionary_formula(self, data, chunks, formula_type, table_key, index_name, extra=None):
        if len(chunks) < 2:
            return None
        table = []
        lookup = {}
        indices = []
        for chunk in chunks:
            idx = lookup.get(chunk)
            if idx is None:
                idx = len(table)
                lookup[chunk] = idx
                table.append(chunk)
            indices.append(idx)

        if len(table) >= len(chunks):
            return None

        codec, encoded = self._encode_indices(indices)
        encoded_table = self._encode_chunk_table(table)
        formula = {
            "type": formula_type,
            "original_size": len(data),
            "sha256": hashlib.sha256(data).hexdigest(),
            f"{table_key}_count": encoded_table["count"],
            f"{table_key}_length_codec": encoded_table["length_codec"],
            f"{table_key}_lengths": encoded_table["lengths"],
            f"{table_key}_data_codec": encoded_table["data_codec"],
            f"{table_key}_data": encoded_table["data"],
            "index_codec": codec,
            "indices": encoded,
            "index_count": len(indices),
            "formula": f"data = concat({table_key}[i] for i in {index_name})",
        }
        if extra:
            formula.update(extra)
        return formula, b""

    def _repeated_lines_formula(self, data):
        max_bytes = self._env_int("KOLIBRI_AGI_REPEATED_LINES_MAX_BYTES", REPEATED_LINES_FORMULA_MAX_BYTES)
        if max_bytes and len(data) > max_bytes:
            return None
        if b"\n" not in data or not self._is_probably_text(data):
            return None
        lines = data.splitlines(keepends=True)
        if len(lines) < 4:
            return None
        return self._dictionary_formula(
            data,
            lines,
            "repeated_lines",
            "lines",
            "line_indices",
            {"line_count": len(lines)},
        )

    def _token_dictionary_formula(self, data):
        max_bytes = self._env_int("KOLIBRI_AGI_TOKEN_FORMULA_MAX_BYTES", TOKEN_FORMULA_MAX_BYTES)
        if max_bytes and len(data) > max_bytes:
            return None
        if len(data) < 64 or not self._is_probably_text(data):
            return None
        tokens = TOKEN_RE.findall(data)
        if len(tokens) < 16:
            return None
        return self._dictionary_formula(
            data,
            tokens,
            "token_dictionary",
            "tokens",
            "token_indices",
            {"token_count": len(tokens), "lexer": "c_like_bytes"},
        )

    def _is_capture_token(self, token):
        if IDENT_RE.fullmatch(token) or NUMBER_RE.fullmatch(token):
            return True
        return len(token) >= 2 and (
            (token.startswith(b'"') and token.endswith(b'"'))
            or (token.startswith(b"'") and token.endswith(b"'"))
        )

    def _c_ast_line_patterns_formula(self, data):
        max_bytes = self._env_int("KOLIBRI_AGI_AST_FORMULA_MAX_BYTES", AST_FORMULA_MAX_BYTES)
        if max_bytes and len(data) > max_bytes:
            return None
        if PLACEHOLDER in data or len(data) < 128 or not self._is_probably_text(data):
            return None
        if not any(marker in data for marker in (b";", b"{", b"}", b"#include", b"typedef", b"struct")):
            return None

        lines = data.splitlines(keepends=True)
        if len(lines) < 8:
            return None

        templates = []
        template_lookup = {}
        template_indices = []
        captures = []
        capture_lookup = {}
        capture_indices = []

        for line in lines:
            parts = []
            line_tokens = TOKEN_RE.findall(line)
            if not line_tokens:
                line_tokens = [line]
            for token in line_tokens:
                if self._is_capture_token(token):
                    parts.append(PLACEHOLDER)
                    idx = capture_lookup.get(token)
                    if idx is None:
                        idx = len(captures)
                        capture_lookup[token] = idx
                        captures.append(token)
                    capture_indices.append(idx)
                else:
                    parts.append(token)
            template = b"".join(parts)
            tidx = template_lookup.get(template)
            if tidx is None:
                tidx = len(templates)
                template_lookup[template] = tidx
                templates.append(template)
            template_indices.append(tidx)

        if len(templates) >= len(lines) or not captures:
            return None

        line_codec, line_payload = self._encode_indices(template_indices)
        capture_codec, capture_payload = self._encode_indices(capture_indices)
        template_table = self._encode_chunk_table(templates)
        capture_table = self._encode_chunk_table(captures)
        formula = {
            "type": "c_ast_line_patterns",
            "original_size": len(data),
            "sha256": hashlib.sha256(data).hexdigest(),
            "template_count": template_table["count"],
            "template_length_codec": template_table["length_codec"],
            "template_lengths": template_table["lengths"],
            "template_data_codec": template_table["data_codec"],
            "template_data": template_table["data"],
            "template_index_codec": line_codec,
            "template_indices": line_payload,
            "line_count": len(template_indices),
            "capture_table_count": capture_table["count"],
            "capture_table_length_codec": capture_table["length_codec"],
            "capture_table_lengths": capture_table["lengths"],
            "capture_table_data_codec": capture_table["data_codec"],
            "capture_table_data": capture_table["data"],
            "capture_index_codec": capture_codec,
            "capture_indices": capture_payload,
            "capture_count": len(capture_indices),
            "formula": "data = render_c_line_templates(templates, captures)",
        }
        return formula, b""


    def _world_model_hologram_formula(self, data):
        if len(data) < 1024:
            return None 
        sha = hashlib.sha256(data).hexdigest()
        wm_path = os.path.join(self.world_model_dir, sha)
        if not os.path.exists(wm_path):
            record = self._build_formula_record(data, skip_hologram=True)
            payload = pickle.dumps(record, protocol=pickle.HIGHEST_PROTOCOL)
            encoded = ATOM_FORMULA_MAGIC + struct.pack(">I", len(payload)) + payload
            with open(wm_path, "wb") as f:
                f.write(encoded)
        return {
            "type": "world_model_hologram",
            "original_size": len(data),
            "sha256": sha,
            "formula": "data = extract_from_world_model(sha256)"
        }, b""

    def _byte_chunk_dictionary_formula(self, data):
        max_bytes = self._env_int("KOLIBRI_AGI_BYTE_CHUNK_FORMULA_MAX_BYTES", BYTE_CHUNK_FORMULA_MAX_BYTES)
        if max_bytes and len(data) > max_bytes:
            return None
        # Universal structural fallback: converts ANY data into a chunk dictionary formula
        for chunk_size in (4096, 1024, 128, 32, 8):
            if len(data) < chunk_size * 2:
                continue
            chunks = [data[i:i+chunk_size] for i in range(0, len(data), chunk_size)]
            cand = self._dictionary_formula(
                data,
                chunks,
                "byte_chunk_dictionary",
                "chunks",
                "chunk_indices",
                {"chunk_size": chunk_size}
            )
            if cand:
                return cand
        return None

    def _build_formula_record(self, data, rel_path=None, skip_hologram=False):
        sha = hashlib.sha256(data).hexdigest()
        best = None
        best_size = None
        best_is_structural = False

        def consider(formula, payload, is_structural=True):
            nonlocal best, best_size, best_is_structural
            record = {"formula": formula, "residual": payload}
            # Use HIGHEST_PROTOCOL for honest size measurement
            record_size = len(pickle.dumps(record, protocol=pickle.HIGHEST_PROTOCOL))
            

            replace = False
            if best is None:
                replace = True
            else:
                if is_structural and not best_is_structural:
                    replace = True
                elif is_structural == best_is_structural:
                    if record_size < best_size:
                        replace = True

            if replace:
                best = record
                best_size = record_size
                best_is_structural = is_structural

        if not skip_hologram and len(data) >= 1024 and not os.environ.get("KOLIBRI_AGI_DISABLE_WORLD_MODEL"):
            candidate = self._world_model_hologram_formula(data)
            if candidate:
                consider(candidate[0], candidate[1], is_structural=True)
                return best

        # 1. Statistical fallback FIRST to set the baseline
        codec, residual = self._compress_residual(data)
        consider({
            "type": "lossless_residual",
            "original_size": len(data),
            "sha256": sha,
            "residual_codec": codec,
            "formula": "data = decode_lossless_residual(residual)",
        }, residual, is_structural=False)
        if (skip_hologram and len(data) >= 1024) or (
            os.environ.get("KOLIBRI_AGI_DISABLE_WORLD_MODEL") and len(data) > 4 * 1024 * 1024
        ):
            return best

        # 2. Base candidates (simple patterns)
        if not data:
            consider({
                "type": "empty",
                "original_size": 0,
                "sha256": sha,
                "formula": "data = b''",
            }, b"")
        elif all(b == data[0] for b in data):
            consider({
                "type": "constant_byte",
                "original_size": len(data),
                "sha256": sha,
                "value": data[0],
                "formula": "data[n] = value",
            }, b"")
        elif len(data) > 1:
            step = (data[1] - data[0]) & 0xFF
            if all(data[i] == ((data[0] + i * step) & 0xFF) for i in range(len(data))):
                consider({
                    "type": "arithmetic_mod256",
                    "original_size": len(data),
                    "sha256": sha,
                    "start": data[0],
                    "step": step,
                    "formula": "data[n] = (start + n * step) mod 256",
                }, b"")

        # 3. Periodic patterns
        periodic = self._periodic_pattern(data)
        if periodic:
            pattern, repeats, tail = periodic
            consider({
                "type": "periodic_pattern",
                "original_size": len(data),
                "sha256": sha,
                "pattern": pattern,
                "repeats": repeats,
                "tail": tail,
                "formula": "data = pattern * repeats + tail",
            }, b"")

        # 4. Path-aware structural builders
        ext = ""
        if rel_path:
            ext = os.path.splitext(rel_path)[1].lower()

        is_source = ext in (".c", ".h", ".cpp", ".hpp", ".py", ".js", ".ts", ".md", ".txt", ".json", ".html", ".css", ".yaml", ".yml", ".sh", ".toml", ".rs", ".go")
        is_c_family = ext in (".c", ".h", ".cpp", ".hpp", ".js", ".ts", ".java", ".cs", ".go", ".rs", ".py") # Added Python to C-like AST family

        builders = []
        
        # LEVEL 6: World Model Hologram (Absolute Priority)
        # FORCE CHECK: skip_hologram MUST BE TRUE FOR STANDALONE VAULTS
        if not skip_hologram and os.environ.get("KOLIBRI_AGI_DISABLE_WM") != "1":
            builders.append(self._world_model_hologram_formula)

        if not rel_path or is_source:
            builders.append(self._repeated_lines_formula)
            builders.append(self._token_dictionary_formula)
        if not rel_path or is_c_family:
            builders.append(self._c_ast_line_patterns_formula)
        
        # Universal structural fallback
        builders.append(self._byte_chunk_dictionary_formula)

        for builder in builders:
            candidate = builder(data)
            if candidate:
                consider(candidate[0], candidate[1], is_structural=True)

        return best

    def _cloud_fetch_knowledge(self, sha):
        import shutil
        remote_path = os.path.join("/tmp/kolibri_cloud_storage/world_model", sha)
        local_path = os.path.join(self.world_model_dir, sha)
        print(f"[*] Cloud Sim: Fetching atom {sha[:16]} from simulated cloud...")
        try:
            shutil.copy2(remote_path, local_path)
            return True
        except Exception:
            return False

    def _materialize_formula_record(self, record):
        formula = record.get("formula", {})
        ftype = formula.get("type")
        size = int(formula.get("original_size", 0))

        if ftype == "empty":
            data = b""
        elif ftype == "constant_byte":
            data = bytes([int(formula["value"]) & 0xFF]) * size
        elif ftype == "arithmetic_mod256":
            start = int(formula["start"]) & 0xFF
            step = int(formula["step"]) & 0xFF
            data = bytes(((start + i * step) & 0xFF) for i in range(size))
        elif ftype == "periodic_pattern":
            pattern = formula["pattern"]
            tail = formula.get("tail", b"")
            data = pattern * int(formula["repeats"]) + tail
        elif ftype in ("repeated_lines", "token_dictionary", "byte_chunk_dictionary"):
            if ftype == "repeated_lines":
                table = self._decode_chunk_table(formula, "lines", "lines")
            elif ftype == "byte_chunk_dictionary":
                table = self._decode_chunk_table(formula, "chunks", "chunks")
            else:
                table = self._decode_chunk_table(formula, "tokens", "tokens")
            indices = self._decode_indices(
                formula["index_codec"],
                formula["indices"],
                int(formula["index_count"]),
            )
            data = b"".join(table[i] for i in indices)
        elif ftype == "c_ast_line_patterns":
            template_indices = self._decode_indices(
                formula["template_index_codec"],
                formula["template_indices"],
                int(formula["line_count"]),
            )
            capture_indices = self._decode_indices(
                formula["capture_index_codec"],
                formula["capture_indices"],
                int(formula["capture_count"]),
            )
            templates = self._decode_chunk_table(formula, "template", "templates")
            captures = self._decode_chunk_table(formula, "capture_table", "captures")
            capture_cursor = 0
            out = bytearray()
            for template_index in template_indices:
                template = templates[template_index]
                parts = template.split(PLACEHOLDER)
                for idx, part in enumerate(parts):
                    out.extend(part)
                    if idx + 1 < len(parts):
                        if capture_cursor >= len(capture_indices):
                            raise ValueError("AST-pattern formula: не хватает captures")
                        out.extend(captures[capture_indices[capture_cursor]])
                        capture_cursor += 1
            if capture_cursor != len(capture_indices):
                raise ValueError("AST-pattern formula: лишние captures")
            data = bytes(out)
        elif ftype == "world_model_hologram":
            wm_path = os.path.join(self.world_model_dir, formula["sha256"])
            if not os.path.exists(wm_path):
                # CLOUD RESOLVER TRIGGER
                if not self._cloud_fetch_knowledge(formula["sha256"]):
                    raise ValueError(f"CRITICAL: AGI World Model missing holographic memory {formula['sha256']}")
            with open(wm_path, "rb") as f:
                wm_blob = f.read()
            if wm_blob.startswith(ATOM_FORMULA_MAGIC):
                data = self._decode_atom_formula(wm_blob)
            else:
                data = wm_blob
        elif ftype == "lossless_residual":
            data = self._decompress_residual(formula["residual_codec"], record.get("residual", b""))
        else:
            raise ValueError(f"Неизвестный тип формулы атома: {ftype}")

        if len(data) != size:
            raise ValueError(f"Формула восстановила неверный размер: {len(data)} != {size}")
        if hashlib.sha256(data).hexdigest() != formula.get("sha256"):
            raise ValueError("Формула восстановила данные с неверным SHA-256")
        return data

    def _encode_atom_formula(self, data, rel_path=None, skip_hologram=False):
        record = self._build_formula_record(data, rel_path=rel_path, skip_hologram=skip_hologram)
        payload = pickle.dumps(record, protocol=pickle.HIGHEST_PROTOCOL)
        return ATOM_FORMULA_MAGIC + struct.pack(">I", len(payload)) + payload

    def _decode_atom_formula(self, atom_data):
        if not atom_data.startswith(ATOM_FORMULA_MAGIC):
            try:
                if self.native:
                    return self._quiet_native_call(self.native.decompress, atom_data)[0]
            except Exception:
                pass
            return zlib.decompress(atom_data)

        cursor = len(ATOM_FORMULA_MAGIC)
        if len(atom_data) < cursor + 4:
            raise ValueError("Поврежден atom formula: нет длины")
        payload_len = struct.unpack(">I", atom_data[cursor:cursor + 4])[0]
        cursor += 4
        payload = atom_data[cursor:cursor + payload_len]
        if len(payload) != payload_len:
            raise ValueError("Поврежден atom formula: неполный payload")
        return self._materialize_formula_record(pickle.loads(payload))

    def _inspect_atom_formula(self, atom_data):
        if not atom_data.startswith(ATOM_FORMULA_MAGIC):
            return {
                "type": "legacy_residual",
                "formula": "data = decode_legacy_atom(atom)",
                "atom_bytes": len(atom_data),
                "record_payload_bytes": len(atom_data),
                "pure_formula_bytes": 0,
                "residual_bytes": len(atom_data),
            }
        cursor = len(ATOM_FORMULA_MAGIC)
        payload_len = struct.unpack(">I", atom_data[cursor:cursor + 4])[0]
        payload = atom_data[cursor + 4:cursor + 4 + payload_len]
        record = pickle.loads(payload)
        formula = record.get("formula", {})
        residual_bytes = len(record.get("residual", b""))
        summary = {
            "type": formula.get("type", "unknown"),
            "formula": formula.get("formula", ""),
            "original_size": int(formula.get("original_size", 0) or 0),
            "sha256": formula.get("sha256", ""),
            "residual_bytes": residual_bytes,
            "pure_formula_bytes": max(0, payload_len - residual_bytes),
            "record_payload_bytes": payload_len,
            "atom_bytes": len(atom_data),
        }
        ftype = summary["type"]
        if ftype == "lossless_residual":
            summary["residual_codec"] = formula.get("residual_codec")
        elif ftype == "constant_byte":
            summary["value"] = formula.get("value")
        elif ftype == "arithmetic_mod256":
            summary["start"] = formula.get("start")
            summary["step"] = formula.get("step")
        elif ftype == "periodic_pattern":
            summary["pattern_bytes"] = len(formula.get("pattern", b""))
            summary["repeats"] = formula.get("repeats")
            summary["tail_bytes"] = len(formula.get("tail", b""))
        elif ftype == "byte_chunk_dictionary":
            summary["unique_chunks"] = formula.get("chunks_count", 0)
            summary["chunk_count"] = formula.get("index_count", 0)
            summary["index_bytes"] = len(formula.get("indices", b""))
        elif ftype == "repeated_lines":
            summary["unique_lines"] = formula.get("lines_count", len(formula.get("lines", [])))
            summary["line_count"] = formula.get("line_count")
            summary["index_bytes"] = len(formula.get("indices", b""))
        elif ftype == "token_dictionary":
            summary["unique_tokens"] = formula.get("tokens_count", len(formula.get("tokens", [])))
            summary["token_count"] = formula.get("token_count")
            summary["index_bytes"] = len(formula.get("indices", b""))
        elif ftype == "c_ast_line_patterns":
            summary["templates"] = formula.get("template_count", len(formula.get("templates", [])))
            summary["captures"] = formula.get("capture_table_count", len(formula.get("captures", [])))
            summary["line_count"] = formula.get("line_count")
            summary["capture_count"] = formula.get("capture_count")
            summary["template_index_bytes"] = len(formula.get("template_indices", b""))
            summary["capture_index_bytes"] = len(formula.get("capture_indices", b""))
        return summary

    def _world_model_ref_from_atom(self, atom_data):
        if not atom_data.startswith(ATOM_FORMULA_MAGIC):
            return None
        cursor = len(ATOM_FORMULA_MAGIC)
        if len(atom_data) < cursor + 4:
            return None
        payload_len = struct.unpack(">I", atom_data[cursor:cursor + 4])[0]
        payload = atom_data[cursor + 4:cursor + 4 + payload_len]
        if len(payload) != payload_len:
            return None
        record = pickle.loads(payload)
        formula = record.get("formula", {})
        if formula.get("type") != "world_model_hologram":
            return None
        return formula.get("sha256")

    def _encode_world_model_blob_file(self, wm_hash):
        wm_path = os.path.join(self.world_model_dir, wm_hash)
        with open(wm_path, "rb") as f:
            prefix = f.read(len(ATOM_FORMULA_MAGIC))
            f.seek(0)
            if prefix.startswith(ATOM_FORMULA_MAGIC):
                atom_data = f.read()
                summary = self._inspect_atom_formula(atom_data)
                if summary.get("sha256") != wm_hash:
                    raise ValueError(f"World model formula указывает не на тот SHA-256: {wm_hash}")
                return {
                    "kind": "atom_formula",
                    "codec": "store",
                    "data": atom_data,
                    "orig_size": int(summary.get("original_size", 0) or 0),
                    "sha256": wm_hash,
                }

        raw_size = os.path.getsize(wm_path)
        level = 6 if raw_size > 16 * 1024 * 1024 else 9
        compressor = zlib.compressobj(level)
        digest = hashlib.sha256()
        chunks = []
        with open(wm_path, "rb") as f:
            while True:
                chunk = f.read(1024 * 1024)
                if not chunk:
                    break
                digest.update(chunk)
                chunks.append(compressor.compress(chunk))
        chunks.append(compressor.flush())
        if digest.hexdigest() != wm_hash:
            raise ValueError(f"World model blob поврежден: {wm_hash}")
        zdata = b"".join(chunks)
        if len(zdata) < raw_size:
            return {
                "kind": "raw",
                "codec": "zlib",
                "data": zdata,
                "orig_size": raw_size,
                "sha256": wm_hash,
            }
        with open(wm_path, "rb") as f:
            raw = f.read()
        return {
            "kind": "raw",
            "codec": "store",
            "data": raw,
            "orig_size": raw_size,
            "sha256": wm_hash,
        }

    def _decode_world_model_blob(self, wm_hash, blob):
        if isinstance(blob, bytes):
            data = blob
            orig_size = len(data)
        else:
            kind = blob.get("kind", "raw")
            codec = blob.get("codec", "store")
            payload = blob.get("data", b"")
            orig_size = int(blob.get("orig_size", 0) or 0)
            if blob.get("sha256") and blob["sha256"] != wm_hash:
                raise ValueError(f"World model blob имеет неверный ключ: {wm_hash}")
            if codec == "store":
                data = payload
            elif codec == "zlib":
                data = zlib.decompress(payload)
            else:
                raise ValueError(f"Неизвестный world model codec: {codec}")
            if kind == "atom_formula":
                summary = self._inspect_atom_formula(data)
                if summary.get("sha256") != wm_hash:
                    raise ValueError(f"World model formula повреждена в .bin: {wm_hash}")
                if int(summary.get("original_size", 0) or 0) != orig_size:
                    raise ValueError(f"World model formula имеет неверный размер: {wm_hash}")
                return data
        if len(data) != orig_size:
            raise ValueError(f"World model blob восстановил неверный размер: {len(data)} != {orig_size}")
        if hashlib.sha256(data).hexdigest() != wm_hash:
            raise ValueError(f"World model blob поврежден в .bin: {wm_hash}")
        return data

    def _world_model_blob_sizes(self, blob):
        if isinstance(blob, bytes):
            return len(blob), len(blob)
        return int(blob.get("orig_size", 0) or 0), len(blob.get("data", b""))

    def _finalize_world_model_meta_corpus(
        self,
        meta_type,
        meta_source,
        formula,
        chunks,
        entries,
        raw_corpus_bytes,
        source_payload_bytes,
    ):
        chunk_data = b"".join(chunks)
        chunk_data_codec, chunk_data_payload = self._encode_blob(chunk_data)
        lengths = [len(chunk) for chunk in chunks]
        length_codec, length_payload = self._encode_blob(self._pack_uvarints(lengths))
        index_bytes = sum(len(entry["indices"]) for entry in entries.values())
        meta = {
            "type": meta_type,
            "meta_source": meta_source,
            "formula": formula,
            "chunk_size": self._env_int("KOLIBRI_AGI_WM_META_CHUNK_SIZE", 4096) or 4096,
            "blob_count": len(entries),
            "chunk_count": len(chunks),
            "raw_corpus_bytes": raw_corpus_bytes,
            "source_payload_bytes": source_payload_bytes,
            "unique_chunk_bytes": len(chunk_data),
            "chunk_payload_bytes": len(chunk_data_payload),
            "index_bytes": index_bytes,
            "chunk_length_codec": length_codec,
            "chunk_lengths": length_payload,
            "chunk_data_codec": chunk_data_codec,
            "chunk_data": chunk_data_payload,
            "entries": entries,
            "meta_payload_bytes": 0,
        }
        last_size = -1
        for _ in range(4):
            size = len(pickle.dumps(meta, protocol=pickle.HIGHEST_PROTOCOL))
            if size == last_size:
                break
            meta["meta_payload_bytes"] = size
            last_size = size
        return meta

    def _encode_world_model_payload_meta_corpus(self, world_model):
        chunk_size = self._env_int("KOLIBRI_AGI_WM_META_CHUNK_SIZE", 4096) or 4096
        chunks = []
        lookup = {}
        entries = {}
        raw_corpus_bytes = 0
        source_payload_bytes = 0

        for wm_hash, blob in sorted(world_model.items()):
            if isinstance(blob, bytes):
                kind = "raw"
                codec = "store"
                payload = blob
                orig_size = len(blob)
            else:
                kind = blob.get("kind", "raw")
                codec = blob.get("codec", "store")
                payload = blob.get("data", b"")
                orig_size = int(blob.get("orig_size", 0) or 0)

            raw_corpus_bytes += orig_size
            source_payload_bytes += len(payload)

            indices = []
            for offset in range(0, len(payload), chunk_size):
                chunk = payload[offset:offset + chunk_size]
                idx = lookup.get(chunk)
                if idx is None:
                    idx = len(chunks)
                    lookup[chunk] = idx
                    chunks.append(chunk)
                indices.append(idx)

            index_codec, index_payload = self._encode_indices(indices)
            entries[wm_hash] = {
                "kind": kind,
                "codec": codec,
                "orig_size": orig_size,
                "payload_size": len(payload),
                "payload_sha256": hashlib.sha256(payload).hexdigest(),
                "index_codec": index_codec,
                "indices": index_payload,
                "index_count": len(indices),
            }

        return self._finalize_world_model_meta_corpus(
            "world_model_meta_corpus_v1",
            "payload_chunks",
            "world_model_payload[sha] = concat(chunk_table[i] for i in payload_indices[sha])",
            chunks,
            entries,
            raw_corpus_bytes,
            source_payload_bytes,
        )

    def _encode_world_model_raw_meta_corpus(self, world_model):
        chunk_size = self._env_int("KOLIBRI_AGI_WM_META_CHUNK_SIZE", 4096) or 4096
        chunks = []
        lookup = {}
        entries = {}
        raw_corpus_bytes = 0
        source_payload_bytes = 0

        for wm_hash, blob in sorted(world_model.items()):
            if isinstance(blob, bytes):
                raw = blob
                raw_size = len(raw)
                payload_size = len(blob)
            else:
                raw_size, payload_size = self._world_model_blob_sizes(blob)
                raw = self._decode_atom_formula(blob["data"]) if blob.get("kind") == "atom_formula" else self._decode_world_model_blob(wm_hash, blob)
            source_payload_bytes += payload_size
            if len(raw) != raw_size:
                raise ValueError(f"World model raw meta: неверный размер raw для {wm_hash}")
            if hashlib.sha256(raw).hexdigest() != wm_hash:
                raise ValueError(f"World model raw meta: raw поврежден для {wm_hash}")
            raw_corpus_bytes += len(raw)

            indices = []
            for offset in range(0, len(raw), chunk_size):
                chunk = raw[offset:offset + chunk_size]
                idx = lookup.get(chunk)
                if idx is None:
                    idx = len(chunks)
                    lookup[chunk] = idx
                    chunks.append(chunk)
                indices.append(idx)

            index_codec, index_payload = self._encode_indices(indices)
            entries[wm_hash] = {
                "kind": "raw",
                "codec": "store",
                "orig_size": len(raw),
                "payload_size": len(raw),
                "payload_sha256": wm_hash,
                "index_codec": index_codec,
                "indices": index_payload,
                "index_count": len(indices),
            }

        return self._finalize_world_model_meta_corpus(
            "world_model_meta_corpus_v1",
            "raw_chunks",
            "world_model_raw[sha] = concat(chunk_table[i] for i in raw_indices[sha])",
            chunks,
            entries,
            raw_corpus_bytes,
            source_payload_bytes,
        )

    def _encode_world_model_meta_corpus(self, world_model):
        candidates = [self._encode_world_model_payload_meta_corpus(world_model)]
        if not os.environ.get("KOLIBRI_AGI_DISABLE_WM_RAW_META"):
            candidates.append(self._encode_world_model_raw_meta_corpus(world_model))
        return min(candidates, key=lambda item: self._world_model_container_stats(item)["payload_bytes"])

    def _decode_world_model_meta_corpus(self, meta):
        if not meta:
            return {}
        if meta.get("type") != "world_model_meta_corpus_v1":
            raise ValueError(f"Неизвестный world model meta-corpus: {meta.get('type')}")

        count = int(meta.get("chunk_count", 0) or 0)
        lengths = self._unpack_uvarints(
            self._decode_blob(meta["chunk_length_codec"], meta["chunk_lengths"]),
            count,
        )
        chunk_data = self._decode_blob(meta.get("chunk_data_codec", "store"), meta.get("chunk_data", b""))
        chunks = []
        cursor = 0
        for length in lengths:
            chunk = chunk_data[cursor:cursor + length]
            if len(chunk) != length:
                raise ValueError("World model meta-corpus: повреждена таблица чанков")
            chunks.append(chunk)
            cursor += length
        if cursor != len(chunk_data):
            raise ValueError("World model meta-corpus: лишние байты в таблице чанков")

        world_model = {}
        for wm_hash, entry in meta.get("entries", {}).items():
            indices = self._decode_indices(
                entry["index_codec"],
                entry["indices"],
                int(entry["index_count"]),
            )
            try:
                payload = b"".join(chunks[i] for i in indices)
            except IndexError as exc:
                raise ValueError(f"World model meta-corpus: неверный индекс чанка для {wm_hash}") from exc
            if len(payload) != int(entry.get("payload_size", 0) or 0):
                raise ValueError(f"World model meta-corpus: неверный размер payload для {wm_hash}")
            if hashlib.sha256(payload).hexdigest() != entry.get("payload_sha256"):
                raise ValueError(f"World model meta-corpus: payload поврежден для {wm_hash}")
            world_model[wm_hash] = {
                "kind": entry.get("kind", "raw"),
                "codec": entry.get("codec", "store"),
                "data": payload,
                "orig_size": int(entry.get("orig_size", 0) or 0),
                "sha256": wm_hash,
            }
        return world_model

    def _world_model_container_stats(self, corpus_or_world_model):
        if isinstance(corpus_or_world_model, dict) and corpus_or_world_model.get("type") == "world_model_meta_corpus_v1":
            meta = corpus_or_world_model
            return {
                "mode": "meta_corpus",
                "meta_source": meta.get("meta_source", "payload_chunks"),
                "blobs": int(meta.get("blob_count", 0) or 0),
                "raw_bytes": int(meta.get("raw_corpus_bytes", 0) or 0),
                "payload_bytes": int(meta.get("meta_payload_bytes", 0) or 0),
                "source_payload_bytes": int(meta.get("source_payload_bytes", 0) or 0),
                "unique_chunk_bytes": int(meta.get("unique_chunk_bytes", 0) or 0),
                "chunk_payload_bytes": int(meta.get("chunk_payload_bytes", meta.get("unique_chunk_bytes", 0)) or 0),
                "chunk_count": int(meta.get("chunk_count", 0) or 0),
                "index_bytes": int(meta.get("index_bytes", 0) or 0),
            }

        world_model = corpus_or_world_model or {}
        raw_bytes = 0
        payload_bytes = 0
        for blob in world_model.values():
            raw_size, payload_size = self._world_model_blob_sizes(blob)
            raw_bytes += raw_size
            payload_bytes += payload_size
        return {
            "mode": "blobs",
            "meta_source": "",
            "blobs": len(world_model),
            "raw_bytes": raw_bytes,
            "payload_bytes": payload_bytes,
            "source_payload_bytes": payload_bytes,
            "unique_chunk_bytes": payload_bytes,
            "chunk_payload_bytes": payload_bytes,
            "chunk_count": 0,
            "index_bytes": 0,
        }

    def _write_atom(self, data, rel_path=None, skip_hologram=False):
        h = hashlib.sha256(data).hexdigest()
        atom_path = os.path.join(self.atoms_dir, h)
        rewrite = True
        if os.path.exists(atom_path):
            try:
                with open(atom_path, "rb") as existing:
                    rewrite = not existing.read(len(ATOM_FORMULA_MAGIC)).startswith(ATOM_FORMULA_MAGIC)
            except OSError:
                rewrite = True
        if rewrite:
            tmp_path = atom_path + ".tmp"
            with open(tmp_path, "wb") as fo:
                fo.write(self._encode_atom_formula(data, rel_path=rel_path, skip_hologram=skip_hologram))
            os.replace(tmp_path, atom_path)
        return h

    def _formula_digit_encode(self, corpus):
        payload = pickle.dumps(corpus, protocol=pickle.HIGHEST_PROTOCOL)
        native_max = int(os.environ.get("KOLIBRI_AGI_NATIVE_MAX_BYTES", str(8 * 1024 * 1024)))
        if self.native and len(payload) <= native_max:
            digit_stream, stats = self._quiet_native_call(self.native.compress, payload)
            magic = FORMULA_DIGIT_MAGIC_V2
            root_formula = f"corpus = pickle.loads(kolibri.decompress(concat(D_i))) [ratio={stats.compression_ratio:.2f}x]"
            encoding = "kolibri.formula_digit.v2"
        else:
            level = 6 if len(payload) > 16 * 1024 * 1024 else 9
            digit_stream = zlib.compress(payload, level)
            magic = FORMULA_DIGIT_MAGIC
            root_formula = "corpus = pickle.loads(zlib.inflate(concat(D_i)))"
            encoding = "kolibri.formula_digit.v1"

        blocks = []
        for offset in range(0, len(digit_stream), FORMULA_DIGIT_BLOCK_SIZE):
            chunk = digit_stream[offset:offset + FORMULA_DIGIT_BLOCK_SIZE]
            blocks.append({
                "index": len(blocks),
                "offset": offset,
                "digit_count": len(chunk),
                "sha256": hashlib.sha256(chunk).hexdigest(),
                "formula": "D_i[n]=byte_at(offset+n), base=256",
            })

        header = {
            "encoding": encoding,
            "digit_base": 256,
            "formula_count": len(blocks) + 1,
            "root_formula": root_formula,
            "payload_bytes": len(payload),
            "payload_sha256": hashlib.sha256(payload).hexdigest(),
            "digit_count": len(digit_stream),
            "digit_sha256": hashlib.sha256(digit_stream).hexdigest(),
            "block_size": FORMULA_DIGIT_BLOCK_SIZE,
            "blocks": blocks,
        }
        header_data = json.dumps(header, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")
        return magic + struct.pack(">I", len(header_data)) + header_data + digit_stream

    def _formula_digit_decode(self, data):
        if not (data.startswith(FORMULA_DIGIT_MAGIC) or data.startswith(FORMULA_DIGIT_MAGIC_V2)):
            # Try native decompression first for raw data if possible, else fallback to zlib
            try:
                if self.native:
                    payload, stats = self._quiet_native_call(self.native.decompress, data)
                    corpus = pickle.loads(payload)
                    return corpus, {
                        "encoding": "kolibri.native",
                        "payload_bytes": len(payload),
                        "digit_count": len(data),
                        "digit_base": 256,
                        "formula_count": 1,
                        "header_bytes": 0,
                    }
            except:
                pass
            
            payload = zlib.decompress(data)
            corpus = pickle.loads(payload)
            return corpus, {
                "encoding": "legacy.zlib_pickle",
                "payload_bytes": len(payload),
                "digit_count": len(data),
                "digit_base": 256,
                "formula_count": 1,
                "header_bytes": 0,
            }

        is_v2 = data.startswith(FORMULA_DIGIT_MAGIC_V2)
        magic_len = len(FORMULA_DIGIT_MAGIC_V2) if is_v2 else len(FORMULA_DIGIT_MAGIC)
        
        cursor = magic_len
        if len(data) < cursor + 4:
            raise ValueError("Поврежден formula-digit corpus: нет длины заголовка")
        header_len = struct.unpack(">I", data[cursor:cursor + 4])[0]
        cursor += 4
        header_data = data[cursor:cursor + header_len]
        digit_stream = data[cursor + header_len:]
        if len(header_data) != header_len:
            raise ValueError("Поврежден formula-digit corpus: неполный заголовок")
        header = json.loads(header_data.decode("utf-8"))
        if header.get("digit_sha256") != hashlib.sha256(digit_stream).hexdigest():
            raise ValueError("Поврежден formula-digit corpus: цифровой поток не совпадает с хешем")
        if header.get("digit_count") != len(digit_stream):
            raise ValueError("Поврежден formula-digit corpus: неверная длина цифрового потока")

        for block in header.get("blocks", []):
            offset = int(block["offset"])
            digit_count = int(block["digit_count"])
            chunk = digit_stream[offset:offset + digit_count]
            if len(chunk) != digit_count or hashlib.sha256(chunk).hexdigest() != block["sha256"]:
                raise ValueError(f"Поврежден formula-digit corpus: блок {block.get('index')} не совпадает")

        if is_v2 and self.native:
            payload, _ = self._quiet_native_call(self.native.decompress, digit_stream)
        else:
            payload = zlib.decompress(digit_stream)
            
        if header.get("payload_sha256") != hashlib.sha256(payload).hexdigest():
            raise ValueError("Поврежден formula-digit corpus: payload не совпадает с хешем")
        if header.get("payload_bytes") != len(payload):
            raise ValueError("Поврежден formula-digit corpus: неверная длина payload")
        header["header_bytes"] = magic_len + 4 + header_len
        return pickle.loads(payload), header

    def inspect_corpus_file(self, corpus_path):
        with open(corpus_path, "rb") as f:
            data = f.read()
        if data.startswith(FORMULA_DIGIT_MAGIC) or data.startswith(FORMULA_DIGIT_MAGIC_V2):
            is_v2 = data.startswith(FORMULA_DIGIT_MAGIC_V2)
            magic_len = len(FORMULA_DIGIT_MAGIC_V2) if is_v2 else len(FORMULA_DIGIT_MAGIC)
            cursor = magic_len
            header_len = struct.unpack(">I", data[cursor:cursor + 4])[0]
            cursor += 4
            header = json.loads(data[cursor:cursor + header_len].decode("utf-8"))
            header["header_bytes"] = magic_len + 4 + header_len
            header["corpus_bytes"] = len(data)
            return header
        
        # Fallback to legacy/native raw
        try:
            if self.native:
                payload, stats = self._quiet_native_call(self.native.decompress, data)
                return {
                    "encoding": "kolibri.native",
                    "payload_bytes": len(payload),
                    "digit_count": len(data),
                    "digit_base": 256,
                    "formula_count": 1,
                    "header_bytes": 0,
                    "corpus_bytes": len(data),
                }
        except:
            pass

        payload = zlib.decompress(data)
        return {
            "encoding": "legacy.zlib_pickle",
            "payload_bytes": len(payload),
            "digit_count": len(data),
            "digit_base": 256,
            "formula_count": 1,
            "header_bytes": 0,
            "corpus_bytes": len(data),
        }

    def pack(self, path, seed_name, skip_hologram=None):
        if skip_hologram is None:
            skip_hologram = os.environ.get("KOLIBRI_AGI_DISABLE_WM") == "1"
        root_path = os.path.abspath(os.path.expanduser(path))
        print(f"[*] Глобальный анализ (Метаформулы / Solid Blocks): {root_path}")
        
        project_map = {}
        dirs_meta = {}
        file_meta = {}
        symlinks = {}
        errors = []
        source_name = os.path.basename(root_path.rstrip(os.sep)) or "root"
        processed_files = 0
        processed_bytes = 0
        
        ignore_patterns = [
            ".git", "__pycache__", "restored_", ".bin", ".seed", ".json", 
            "build", "dist", ".tmp", ".mov", ".gz", ".bz2", "total_vault"
        ]

        def should_ignore(name):
            for p in ignore_patterns:
                if p in name: return True
            return False

        def note_processed(size):
            nonlocal processed_files, processed_bytes
            processed_files += 1
            processed_bytes += size

        if os.path.islink(root_path):
            raise RuntimeError("Корневой symlink не поддержан как источник архива")

        solid_blocks = os.environ.get("KOLIBRI_AGI_SOLID_BLOCKS") == "1"
        meta_blocks = {}
        
        def add_file_record(rel, data, st):
            nonlocal processed_files, processed_bytes
            processed_files += 1
            processed_bytes += len(data)
            if processed_files % 1000 == 0 or processed_bytes // (100 * 1024 * 1024) != (processed_bytes - len(data)) // (100 * 1024 * 1024):
                print(f"[*] Упаковано файлов: {processed_files}, данных: {processed_bytes} bytes")
            if not solid_blocks:
                block_hash = self._write_atom(data, rel_path=rel)
                project_map[rel] = {
                    "block": block_hash,
                    "offset": 0,
                    "length": len(data),
                }
                if st:
                    file_meta[rel] = {
                        "mode": stat.S_IMODE(st.st_mode),
                        "mtime_ns": getattr(st, "st_mtime_ns", int(st.st_mtime * 1_000_000_000)),
                        "size": st.st_size,
                    }
                return

            ext = os.path.splitext(rel)[1].lower()
            if ext in ('.c', '.h', '.cpp', '.hpp', '.go', '.rs'):
                group = 'meta_c'
            elif ext in ('.py', '.sh', '.js', '.ts'):
                group = 'meta_script'
            elif self._is_probably_text(data):
                group = 'meta_text'
            else:
                group = 'meta_bin'
                
            if group not in meta_blocks:
                meta_blocks[group] = bytearray()
                
            offset = len(meta_blocks[group])
            meta_blocks[group].extend(data)
            project_map[rel] = {
                "group": group,
                "offset": offset,
                "length": len(data),
            }
            if st:
                file_meta[rel] = {
                    "mode": stat.S_IMODE(st.st_mode),
                    "mtime_ns": getattr(st, "st_mtime_ns", int(st.st_mtime * 1_000_000_000)),
                    "size": st.st_size,
                }

        if os.path.isfile(root_path):
            st = os.stat(root_path)
            with open(root_path, "rb") as fi:
                add_file_record(source_name, fi.read(), st)
        elif not os.path.isdir(root_path):
            raise FileNotFoundError(root_path)
        else:
            for root, dirs, files in os.walk(root_path, topdown=True, followlinks=False):
                dirs.sort()
                files.sort()

                rel_root = os.path.relpath(root, root_path)
                if rel_root != ".":
                    try:
                        dirs_meta[rel_root] = self._metadata(root)
                    except OSError as e:
                        errors.append((rel_root, str(e)))

                for d in list(dirs):
                    d_path = os.path.join(root, d)
                    rel = os.path.relpath(d_path, root_path)
                    if os.path.islink(d_path):
                        try:
                            symlinks[rel] = {
                                "target": os.readlink(d_path),
                                **self._metadata(d_path, follow_symlinks=False),
                            }
                        except OSError as e:
                            errors.append((rel, str(e)))
                        dirs.remove(d)

                for f in files:
                    f_path = os.path.join(root, f)
                    rel = os.path.relpath(f_path, root_path)
                    try:
                        if os.path.islink(f_path):
                            symlinks[rel] = {
                                "target": os.readlink(f_path),
                                **self._metadata(f_path, follow_symlinks=False),
                            }
                            continue

                        st = os.stat(f_path)
                        if not stat.S_ISREG(st.st_mode):
                            errors.append((rel, "необычный тип файла, не regular file"))
                            continue

                        with open(f_path, "rb") as fi:
                            add_file_record(rel, fi.read(), st)
                    except OSError as e:
                        errors.append((rel, str(e)))

        if errors:
            print(f"[!] Архив не создан: есть непрочитанные/неподдержанные элементы: {len(errors)}")
            for rel, error in errors[:50]:
                print(f"    - {rel}: {error}")
            raise RuntimeError("pack aborted")

        if solid_blocks:
            print(f"[*] Сборка Метаформул (Solid blocks): {list(meta_blocks.keys())}")
            for group, data in meta_blocks.items():
                group_ext = ".c" if group == "meta_c" else (".py" if group == "meta_script" else (".txt" if group == "meta_text" else ".bin"))
                block_hash = self._write_atom(bytes(data), rel_path=f"solid{group_ext}", skip_hologram=skip_hologram)
                for rel, info in project_map.items():
                    if info.get("group") == group:
                        info["block"] = block_hash
                        del info["group"]
            meta_blocks.clear()

        # v50/v40 Cognitive Solid Core is now the default
        archive = {
            ARCHIVE_MARKER: ARCHIVE_VERSION,
            "files": project_map,
            "dirs": dirs_meta,
            "symlinks": symlinks,
            "file_meta": file_meta,
            "is_solid": solid_blocks,
            "level6_active": True,
            "core_version": "v61.portable_world_model_solid" if solid_blocks else "v61.portable_world_model_atoms",
            "fractal_layers": 4,
            "source_name": source_name,
            "source_kind": "file" if os.path.isfile(root_path) else "directory",
        }
        # Compress the archive map (metadata) itself!
        map_raw = pickle.dumps(archive, protocol=pickle.HIGHEST_PROTOCOL)
        print(f"[*] Raw Map Size: {len(map_raw) / 1024:.2f} KB")
        
        native_max = int(os.environ.get("KOLIBRI_AGI_NATIVE_MAX_BYTES", str(8 * 1024 * 1024)))
        if self.native and len(map_raw) <= native_max:
            map_compressed, map_stats = self._quiet_native_call(self.native.compress, map_raw)
            map_codec = "kolibri.native"
            # Use formula digit encoding for the map too
            map_data = self._formula_digit_encode({"type": "map_v2", "codec": map_codec, "data": map_compressed, "orig_size": len(map_raw)})
            print(f"[*] Compressed Map Size: {len(map_data) / 1024:.2f} KB (Ratio: {len(map_raw)/len(map_data):.2f}x)")
        else:
            level = 6 if len(map_raw) > 16 * 1024 * 1024 else 9
            map_compressed = zlib.compress(map_raw, level)
            map_codec = "zlib"
            map_data = self._formula_digit_encode({"type": "map_v2", "codec": map_codec, "data": map_compressed, "orig_size": len(map_raw)})
            print(f"[*] Compressed Map Size: {len(map_data) / 1024:.2f} KB (Ratio: {len(map_raw)/len(map_data):.2f}x)")

        map_hash = hashlib.sha256(map_data).hexdigest()
        map_path = os.path.join(self.maps_dir, map_hash)
        with open(map_path, "wb") as fo:
            fo.write(map_data)

        seed_name = self._seed_name(seed_name)
        seed_path = os.path.join(self.seeds_dir, f"{seed_name}.seed")
        with open(seed_path, "wb") as f:
            f.write(bytes.fromhex(map_hash))

        print(f"[*] Файлов в карте: {len(project_map)}")
        print(f"[*] Директорий в карте: {len(dirs_meta)}")
        print(f"[*] Symlink в карте: {len(symlinks)}")
        print(f"[+] Семя {seed_name} готово: {seed_path}")

    def _remove_existing_path(self, path):
        if not os.path.lexists(path):
            return
        if os.path.isdir(path) and not os.path.islink(path):
            shutil.rmtree(path)
        else:
            os.unlink(path)

    def _apply_meta(self, path, meta, follow_symlinks=True):
        if not meta:
            return
        mode = meta.get("mode")
        if mode is not None and (follow_symlinks or not os.path.islink(path)):
            try:
                os.chmod(path, mode)
            except OSError:
                pass
        mtime_ns = meta.get("mtime_ns")
        if mtime_ns is not None:
            try:
                os.utime(path, ns=(mtime_ns, mtime_ns), follow_symlinks=follow_symlinks)
            except (OSError, NotImplementedError):
                pass

    def unpack(self, seed_ref, target):
        seed_data, seed_name = self._read_seed(seed_ref)
        map_hash = seed_data.hex()
        _, archive = self._load_archive(map_hash)
        target = os.path.abspath(os.path.expanduser(target))

        print(f"[*] Регенерация проекта из seed '{seed_name}'...")
        os.makedirs(target, exist_ok=True)

        for rel, meta in sorted(archive["dirs"].items(), key=lambda item: item[0].count(os.sep)):
            d_path = self._target_path(target, rel)
            os.makedirs(d_path, exist_ok=True)
            self._apply_meta(d_path, meta)

        for rel, link_meta in archive["symlinks"].items():
            link_path = self._target_path(target, rel)
            os.makedirs(os.path.dirname(link_path), exist_ok=True)
            self._remove_existing_path(link_path)
            os.symlink(link_meta["target"], link_path)
            self._apply_meta(link_path, link_meta, follow_symlinks=False)

        # Cache for solid blocks during unpack to avoid re-reading/re-decompressing same block
        block_cache = {}

        for rel, file_info in archive["files"].items():
            f_path = self._target_path(target, rel)
            os.makedirs(os.path.dirname(f_path), exist_ok=True)
            if os.path.lexists(f_path) and os.path.islink(f_path):
                os.unlink(f_path)
            
            # Handle both old (direct hash) and new (dict with solid block) formats
            if isinstance(file_info, str):
                h = file_info
                offset, length = 0, -1
            else:
                h = file_info["block"]
                offset, length = file_info["offset"], file_info["length"]

            if h not in block_cache:
                atom_path = os.path.join(self.atoms_dir, h)
                with open(atom_path, "rb") as fi:
                    atom_data = fi.read()
                    block_data = self._decode_atom_formula(atom_data)
                block_cache[h] = block_data
            
            full_block = block_cache[h]
            if length == -1:
                data = full_block
            else:
                data = full_block[offset:offset + length]
                    
            with open(f_path, "wb") as fo:
                fo.write(data)
            meta = archive["file_meta"].get(rel)
            if meta:
                self._apply_meta(f_path, meta)
            elif rel.endswith((".py", ".sh")) or "bin/" in rel:
                os.chmod(f_path, 0o755)

        for rel, meta in sorted(archive["dirs"].items(), key=lambda item: item[0].count(os.sep), reverse=True):
            self._apply_meta(self._target_path(target, rel), meta)

        print(f"[+] Восстановлено в: {target}")
        print(f"[*] Файлов: {len(archive['files'])}, директорий: {len(archive['dirs'])}, symlink: {len(archive['symlinks'])}")

    def export_pair(self, seed_ref, output_dir):
        os.makedirs(output_dir, exist_ok=True)
        seed_data, seed_name = self._read_seed(seed_ref)
        map_hash = seed_data.hex()
        map_data, archive = self._load_archive(map_hash)

        # Collect all unique atom hashes (handles both string and dict formats)
        atom_hashes = set()
        for f_info in archive["files"].values():
            if isinstance(f_info, str):
                atom_hashes.add(f_info)
            else:
                atom_hashes.add(f_info["block"])

        atoms = {}
        atom_formulas = {}
        world_model = {}
        for atom_hash in sorted(atom_hashes):
            atom_path = os.path.join(self.atoms_dir, atom_hash)
            with open(atom_path, "rb") as f:
                atoms[atom_hash] = f.read()
            atom_formulas[atom_hash] = self._inspect_atom_formula(atoms[atom_hash])
            wm_ref = self._world_model_ref_from_atom(atoms[atom_hash])
            if wm_ref:
                world_model[wm_ref] = self._encode_world_model_blob_file(wm_ref)

        world_model_meta = None
        if world_model and not os.environ.get("KOLIBRI_AGI_DISABLE_WM_META"):
            candidate_meta = self._encode_world_model_meta_corpus(world_model)
            direct_stats = self._world_model_container_stats(world_model)
            meta_stats = self._world_model_container_stats(candidate_meta)
            if os.environ.get("KOLIBRI_AGI_FORCE_WM_META") or meta_stats["payload_bytes"] < direct_stats["payload_bytes"]:
                world_model_meta = candidate_meta

        corpus = {
            "version": ARCHIVE_VERSION,
            "seed_name": seed_name,
            "map_hash": map_hash,
            "map": map_data,
            "atoms": atoms,
            "atom_formulas": atom_formulas,
        }
        if world_model_meta:
            corpus["world_model_meta_corpus"] = world_model_meta
        else:
            corpus["world_model"] = world_model
        corpus_data = self._formula_digit_encode(corpus)

        out_seed = os.path.join(output_dir, f"{seed_name}.seed")
        out_corpus = os.path.join(output_dir, f"{seed_name}.bin")
        with open(out_seed, "wb") as f:
            f.write(seed_data)
        with open(out_corpus, "wb") as f:
            f.write(corpus_data)

        if os.environ.get("KOLIBRI_AGI_FORMULA_REPORT"):
            report_path = os.environ["KOLIBRI_AGI_FORMULA_REPORT"]
            report = []
            for rel, f_info in archive["files"].items():
                if isinstance(f_info, str):
                    h = f_info
                    size = -1
                else:
                    h = f_info["block"]
                    size = f_info["length"]
                
                fstats = atom_formulas.get(h, {})
                report.append({
                    "path": rel,
                    "type": fstats.get("type", "unknown"),
                    "size": size if size != -1 else fstats.get("original_size", 0),
                    "residual_bytes": fstats.get("residual_bytes", 0),
                    "pure_formula_bytes": fstats.get("pure_formula_bytes", 0),
                    "atom_bytes": fstats.get("atom_bytes", 0),
                })
            
            # Sort by residual bytes descending
            report.sort(key=lambda x: x["residual_bytes"], reverse=True)
            with open(report_path, "w", encoding="utf-8") as f:
                json.dump(report, f, indent=2, ensure_ascii=False)
            print(f"[*] Отчет по формулам сохранен в: {report_path}")

        print("[+] Экспортировано 2 файла:")
        print(f"    seed:   {out_seed} ({len(seed_data)} bytes)")
        print(f"    corpus: {out_corpus} ({len(corpus_data)} bytes)")
        info = self.inspect_corpus_file(out_corpus)
        print(f"[*] Корпус: {info['encoding']}")
        print(f"[*] Формул в корпусе: {info['formula_count']}")
        print(f"[*] Цифровая база: {info['digit_base']}")
        print(f"[*] Цифр в потоке: {info['digit_count']}")
        print(f"[*] Файлов в карте: {len(archive['files'])}")
        print(f"[*] Директорий в карте: {len(archive['dirs'])}")
        print(f"[*] Symlink в карте: {len(archive['symlinks'])}")
        print(f"[*] Атомов в корпусе: {len(atoms)}")
        wm_stats = self._world_model_container_stats(world_model_meta or world_model)
        print(f"[*] World model mode: {wm_stats['mode']}")
        print(f"[*] World model blobs: {wm_stats['blobs']}")
        print(f"[*] World model corpus: {wm_stats['raw_bytes']} bytes")
        print(f"[*] World model payload: {wm_stats['payload_bytes']} bytes")
        if wm_stats["mode"] == "meta_corpus":
            print(f"[*] World model meta source: {wm_stats['meta_source']}")
            print(f"[*] World model source payload: {wm_stats['source_payload_bytes']} bytes")
            print(f"[*] World model chunks: {wm_stats['chunk_count']}")
            print(f"[*] World model unique chunks: {wm_stats['unique_chunk_bytes']} bytes")
            print(f"[*] World model chunk payload: {wm_stats['chunk_payload_bytes']} bytes")
            if wm_stats["source_payload_bytes"]:
                print(f"[*] World model meta ratio: {wm_stats['source_payload_bytes'] / max(1, wm_stats['payload_bytes']):.2f}x")
        formula_counts = {}
        residual_bytes = 0
        pure_formula_bytes = 0
        for formula in atom_formulas.values():
            ftype = formula.get("type", "unknown")
            formula_counts[ftype] = formula_counts.get(ftype, 0) + 1
            residual_bytes += int(formula.get("residual_bytes", 0) or 0)
            pure_formula_bytes += int(formula.get("pure_formula_bytes", 0) or 0)
        print(f"[*] Формулы атомов: {formula_counts}")
        print(f"[*] Чистая формула: {pure_formula_bytes} bytes")
        print(f"[*] Lossless residual: {residual_bytes} bytes")
        formula_total = pure_formula_bytes + residual_bytes + wm_stats["payload_bytes"]
        if formula_total:
            print(f"[*] Доля формулы: {pure_formula_bytes / formula_total * 100:.2f}%")
            print(f"[*] Доля residual/world-model: {(residual_bytes + wm_stats['payload_bytes']) / formula_total * 100:.2f}%")
        return out_seed, out_corpus

    def import_pair(self, seed_ref, input_dir):
        seed_name = self._seed_name(seed_ref)
        seed_path = seed_ref if os.path.isfile(seed_ref) else os.path.join(input_dir, f"{seed_name}.seed")
        corpus_path = os.path.join(input_dir, f"{seed_name}.bin")
        if not os.path.exists(corpus_path):
            corpus_path = os.path.join(input_dir, f"{seed_name}.corpus")
        return self.import_pair_paths(seed_path, corpus_path, seed_name)

    def import_pair_paths(self, seed_path, corpus_path, seed_name=None):
        seed_name = self._seed_name(seed_name or seed_path)
        with open(seed_path, "rb") as f:
            seed_data = f.read()
        with open(corpus_path, "rb") as f:
            corpus, corpus_info = self._formula_digit_decode(f.read())

        map_hash = seed_data.hex()
        if corpus.get("map_hash") != map_hash:
            raise ValueError("Seed и corpus не совпадают: разные map hash")

        with open(os.path.join(self.seeds_dir, f"{seed_name}.seed"), "wb") as f:
            f.write(seed_data)
        with open(os.path.join(self.maps_dir, map_hash), "wb") as f:
            f.write(corpus["map"])

        atoms = corpus["atoms"]
        hologram_payloads = atoms.get("__hologram_payloads__", {})
        for sha, payload in hologram_payloads.items():
            wm_path = os.path.join(self.world_model_dir, sha)
            if not os.path.exists(wm_path):
                with open(wm_path, "wb") as fwm:
                    fwm.write(payload)
        
        for atom_hash, atom_data in atoms.items():
            if atom_hash == "__hologram_payloads__": continue
            atom_path = os.path.join(self.atoms_dir, atom_hash)
            if not os.path.exists(atom_path):
                with open(atom_path, "wb") as f:
                    f.write(atom_data)

        world_model_meta = corpus.get("world_model_meta_corpus")
        world_model = self._decode_world_model_meta_corpus(world_model_meta) if world_model_meta else corpus.get("world_model", {})
        wm_stats = self._world_model_container_stats(world_model_meta or world_model)
        for wm_hash, wm_blob in world_model.items():
            wm_data = self._decode_world_model_blob(wm_hash, wm_blob)
            wm_path = os.path.join(self.world_model_dir, wm_hash)
            if not os.path.exists(wm_path):
                with open(wm_path, "wb") as f:
                    f.write(wm_data)

        _, archive = self._load_archive(map_hash)
        print("[+] Импортировано из 2 файлов.")
        print(f"[*] Корпус: {corpus_info['encoding']}")
        print(f"[*] Формул в корпусе: {corpus_info['formula_count']}")
        print(f"[*] Цифр в потоке: {corpus_info['digit_count']}")
        print(f"[*] Файлов в карте: {len(archive['files'])}")
        print(f"[*] Директорий в карте: {len(archive['dirs'])}")
        print(f"[*] Symlink в карте: {len(archive['symlinks'])}")
        print(f"[*] Атомов в корпусе: {len(atoms)}")
        print(f"[*] World model mode: {wm_stats['mode']}")
        print(f"[*] World model blobs: {wm_stats['blobs']}")
        print(f"[*] World model corpus: {wm_stats['raw_bytes']} bytes")
        print(f"[*] World model payload: {wm_stats['payload_bytes']} bytes")
        if wm_stats["mode"] == "meta_corpus":
            print(f"[*] World model meta source: {wm_stats['meta_source']}")
            print(f"[*] World model source payload: {wm_stats['source_payload_bytes']} bytes")
            print(f"[*] World model chunks: {wm_stats['chunk_count']}")
            print(f"[*] World model unique chunks: {wm_stats['unique_chunk_bytes']} bytes")
            print(f"[*] World model chunk payload: {wm_stats['chunk_payload_bytes']} bytes")
        if corpus.get("atom_formulas"):
            formula_counts = {}
            for formula in corpus["atom_formulas"].values():
                ftype = formula.get("type", "unknown")
                formula_counts[ftype] = formula_counts.get(ftype, 0) + 1
            print(f"[*] Формулы атомов: {formula_counts}")

    def pair(self, path, seed_name, output_dir):
        self.pack(path, seed_name)
        return self.export_pair(seed_name, output_dir)

    def restore_pair(self, seed_path, corpus_path, target):
        self.import_pair_paths(seed_path, corpus_path)
        self.unpack(seed_path, target)

    def _pair_formula_stats(self, corpus_path):
        with open(corpus_path, "rb") as f:
            corpus, _ = self._formula_digit_decode(f.read())
        counts = {}
        residual_bytes = 0
        pure_formula_bytes = 0
        atom_bytes = 0
        world_model_meta = corpus.get("world_model_meta_corpus")
        world_model = corpus.get("world_model", {})
        wm_stats = self._world_model_container_stats(world_model_meta or world_model)
        for formula in corpus.get("atom_formulas", {}).values():
            ftype = formula.get("type", "unknown")
            counts[ftype] = counts.get(ftype, 0) + 1
            residual_bytes += int(formula.get("residual_bytes", 0) or 0)
            pure_formula_bytes += int(formula.get("pure_formula_bytes", 0) or 0)
            atom_bytes += int(formula.get("atom_bytes", 0) or 0)
        return {
            "counts": counts,
            "residual_bytes": residual_bytes,
            "pure_formula_bytes": pure_formula_bytes,
            "atom_bytes": atom_bytes,
            "world_model_mode": wm_stats["mode"],
            "world_model_meta_source": wm_stats["meta_source"],
            "world_model_blobs": wm_stats["blobs"],
            "world_model_bytes": wm_stats["raw_bytes"],
            "world_model_payload_bytes": wm_stats["payload_bytes"],
            "world_model_source_payload_bytes": wm_stats["source_payload_bytes"],
            "world_model_unique_chunk_bytes": wm_stats["unique_chunk_bytes"],
            "world_model_chunk_payload_bytes": wm_stats["chunk_payload_bytes"],
            "world_model_chunk_count": wm_stats["chunk_count"],
            "world_model_index_bytes": wm_stats["index_bytes"],
        }

    def roundtrip_pair(self, path, seed_name, output_dir, restore_dir=None):
        original = self._snapshot(path)
        out_seed, out_corpus = self.pair(path, seed_name, output_dir)
        formula_stats = self._pair_formula_stats(out_corpus)
        if restore_dir is None:
            restore_dir = tempfile.mkdtemp(prefix=f"kolibri_restore_{self._seed_name(seed_name)}_")
        else:
            restore_dir = os.path.abspath(os.path.expanduser(restore_dir))
            if os.path.lexists(restore_dir):
                shutil.rmtree(restore_dir)

        with tempfile.TemporaryDirectory(prefix="kolibri_pair_verify_") as tmp_storage:
            verifier = KolibriAGI(storage=tmp_storage)
            verifier.restore_pair(out_seed, out_corpus, restore_dir)

        restored = self._snapshot(restore_dir)
        errors = self._compare_snapshots(original, restored)
        seed_bytes = os.path.getsize(out_seed)
        corpus_bytes = os.path.getsize(out_corpus)
        total_archive = seed_bytes + corpus_bytes
        ratio = (original["bytes"] / total_archive) if total_archive else 0.0

        print("\n=== ЧЕСТНЫЙ ОТЧЕТ SEED+BIN ===")
        print(f"Исходный размер:     {original['bytes']} bytes")
        print(f"Seed:                {seed_bytes} bytes ({out_seed})")
        print(f"Bin corpus:          {corpus_bytes} bytes ({out_corpus})")
        print(f"Seed+bin:            {total_archive} bytes")
        print(f"Коэффициент:         {ratio:.2f}x")
        print(f"Файлов:              {len(original['files'])}")
        print(f"Директорий:          {len(original['dirs'])}")
        print(f"Symlink:             {len(original['symlinks'])}")
        print(f"Формулы атомов:      {formula_stats['counts']}")
        print(f"Чистая формула:      {formula_stats['pure_formula_bytes']} bytes")
        print(f"Lossless residual:   {formula_stats['residual_bytes']} bytes")
        print(f"World model mode:    {formula_stats['world_model_mode']}")
        print(f"World model blobs:   {formula_stats['world_model_blobs']}")
        print(f"World model corpus:  {formula_stats['world_model_bytes']} bytes")
        print(f"World model payload: {formula_stats['world_model_payload_bytes']} bytes")
        if formula_stats["world_model_mode"] == "meta_corpus":
            print(f"WM meta source:      {formula_stats['world_model_meta_source']}")
            print(f"WM source payload:   {formula_stats['world_model_source_payload_bytes']} bytes")
            print(f"WM chunks:           {formula_stats['world_model_chunk_count']}")
            print(f"WM unique chunks:    {formula_stats['world_model_unique_chunk_bytes']} bytes")
            print(f"WM chunk payload:    {formula_stats['world_model_chunk_payload_bytes']} bytes")
            if formula_stats["world_model_source_payload_bytes"]:
                ratio = formula_stats["world_model_source_payload_bytes"] / max(1, formula_stats["world_model_payload_bytes"])
                print(f"WM meta ratio:       {ratio:.2f}x")
        formula_total = (
            formula_stats["pure_formula_bytes"]
            + formula_stats["residual_bytes"]
            + formula_stats["world_model_payload_bytes"]
        )
        if formula_total:
            print(f"Доля формулы:        {formula_stats['pure_formula_bytes'] / formula_total * 100:.2f}%")
            print(f"Доля residual/world: {(formula_stats['residual_bytes'] + formula_stats['world_model_payload_bytes']) / formula_total * 100:.2f}%")
        print(f"Bit-exact:           {'yes' if not errors else 'no'}")
        print(f"Restore path:        {restore_dir}")
        if errors:
            print("[!] Первые расхождения:")
            for error in errors[:20]:
                print(f"    - {error}")
        return not errors


    def shell(self, seed_ref):
        seed_data, seed_name = self._read_seed(seed_ref)
        map_hash = seed_data.hex()
        _, archive = self._load_archive(map_hash)
        
        print(f"\n--- [Level 6 Virtual FS: {seed_name}] ---")
        print("Welcome to Kolibri Level 6 Interactive Shell (Pure Formula Mode).")
        
        current_dir = ""
        cache = {}

        def ls():
            items = set()
            prefix = current_dir + ("/" if current_dir else "")
            for path in archive["files"]:
                if path.startswith(prefix):
                    rel = path[len(prefix):]
                    parts = rel.split("/")
                    items.add(parts[0] + ("/" if len(parts) > 1 else ""))
            for item in sorted(items):
                print(f"  {item}")

        def cat(filename):
            full_path = os.path.join(current_dir, filename)
            if full_path not in archive["files"]:
                print(f"File {filename} not found.")
                return

            file_info = archive["files"][full_path]
            if isinstance(file_info, str):
                h = file_info
                offset = 0
                length = -1
            else:
                h = file_info["block"]
                offset = file_info["offset"]
                length = file_info["length"]

            if h not in cache:
                atom_path = os.path.join(self.atoms_dir, h)
                with open(atom_path, 'rb') as fi:
                    atom_data = fi.read()
                cache[h] = self._decode_atom_formula(atom_data)
            
            if length == -1:
                content = cache[h]
            else:
                content = cache[h][offset:offset+length]
                
            print(f"\n--- {filename} (Extracted from Pure Formula) ---")
            try:
                print(content.decode('utf-8'))
            except:
                print(f"[Binary Data: {len(content)} bytes]")

        while True:
            try:
                prompt = f"kolibri@{seed_name}:{current_dir or '/'}$ "
                cmd_line = input(prompt).strip()
            except (EOFError, KeyboardInterrupt):
                print()
                break
                
            if not cmd_line: continue
            cmd = cmd_line.split()
            
            if cmd[0] == "exit": break
            elif cmd[0] == "ls": ls()
            elif cmd[0] == "cd":
                if len(cmd) < 2:
                    current_dir = ""
                elif cmd[1] == "..":
                    current_dir = "/".join(current_dir.split("/")[:-1])
                elif cmd[1] == "/":
                    current_dir = ""
                else:
                    new_dir = os.path.normpath(os.path.join(current_dir, cmd[1]))
                    if new_dir == ".":
                        current_dir = ""
                    else:
                        current_dir = new_dir.strip("/")
            elif cmd[0] == "cat":
                if len(cmd) > 1:
                    cat(cmd[1])
            elif cmd[0] == "help":
                print("Commands: ls, cd <dir>, cat <file>, exit")
            else:
                print(f"Unknown command: {cmd[0]}")


    def optimize_world_model(self):
        print("[*] Optimizing AGI World Model (Solidifying Knowledge base)...")
        if not os.path.exists(self.world_model_dir):
            print("[!] World Model directory missing.")
            return
            
        entries = os.listdir(self.world_model_dir)
        if not entries:
            print("[!] World Model is empty.")
            return

        print(f"[*] Analyzing {len(entries)} knowledge atoms...")
        # To optimize the world model, we treat it as a project and pack it
        wm_seed = "world_model_v1"
        self.pack(self.world_model_dir, wm_seed, skip_hologram=True)
        print(f"[+] World Model optimized into Meta-Formula '{wm_seed}'")

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("action", choices=["pack", "unpack", "export", "import", "pair", "restore-pair", "roundtrip", "shell", "optimize-world-model"])
    parser.add_argument("--path", nargs="*", help="Source path for pack/pair, or target path(s) for unpack")
    parser.add_argument("--target", nargs="*", help="Target path(s) for unpack/restore")
    parser.add_argument("--seed", required=True)
    parser.add_argument("--bin")
    parser.add_argument("--out", default=".")
    parser.add_argument("--restore")
    args = parser.parse_args()

    agi = KolibriAGI()
    
    source_path = args.path[0] if args.path else None
    targets = args.target if args.target else (args.path if args.path else [])

    if args.action == "pack":
        if not source_path:
            parser.error("--path is required for pack")
        agi.pack(source_path, args.seed)
    elif args.action == "unpack":
        if not targets:
            parser.error("--target or --path is required for unpack")
        for t in targets:
            agi.unpack(args.seed, t)
    elif args.action == "export":
        if not source_path:
            parser.error("--path is required for export (output dir)")
        agi.export_pair(args.seed, source_path)
    elif args.action == "import":
        if not source_path:
            parser.error("--path is required for import (input dir)")
        agi.import_pair(args.seed, source_path)
    elif args.action == "pair":
        if not source_path:
            parser.error("--path is required for pair")
        agi.pair(source_path, args.seed, args.out)
    elif args.action == "restore-pair":
        if not targets:
            parser.error("--target or --path is required for restore-pair")
        corpus_path = args.bin
        if not corpus_path:
            seed_name = agi._seed_name(args.seed)
            seed_dir = os.path.dirname(os.path.abspath(args.seed)) if os.path.isfile(args.seed) else args.out
            corpus_path = os.path.join(seed_dir, f"{seed_name}.bin")
        for t in targets:
            agi.restore_pair(args.seed, corpus_path, t)
    elif args.action == "optimize-world-model":
        agi.optimize_world_model()
    elif args.action == "shell":
        agi.shell(args.seed)
    elif args.action == "roundtrip":
        if not source_path:
            parser.error("--path is required for roundtrip")
        ok = agi.roundtrip_pair(source_path, args.seed, args.out, args.restore)
        if not ok:
            sys.exit(1)


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(f"[!] Ошибка: {exc}", file=sys.stderr)
        sys.exit(1)
