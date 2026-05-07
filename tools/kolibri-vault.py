#!/usr/bin/env python3
import argparse
import hashlib
import importlib.machinery
import importlib.util
import os
import pickle
import struct
import time
import zlib

VAULT_MAGIC = b"KOLIBRI\x02"


class KolibriVault:
    def __init__(self):
        base_dir = os.path.dirname(os.path.abspath(__file__))
        agi_path = os.path.join(base_dir, "kolibri-agi.py")
        loader = importlib.machinery.SourceFileLoader("kolibri_agi", agi_path)
        spec = importlib.util.spec_from_loader(loader.name, loader)
        self.kolibri_agi = importlib.util.module_from_spec(spec)
        loader.exec_module(self.kolibri_agi)
        self.agi = self.kolibri_agi.KolibriAGI()

    def _env_int(self, name, default):
        try:
            value = int(os.environ.get(name, str(default)))
        except ValueError:
            return default
        return value if value > 0 else default

    def _gear_table(self):
        table = getattr(self, "_gear_hash_table", None)
        if table is not None:
            return table

        # Deterministic SplitMix64 table. Stable across Python runs and platforms.
        state = 0x9E3779B97F4A7C15
        table = []
        for _ in range(256):
            state = (state + 0x9E3779B97F4A7C15) & 0xFFFFFFFFFFFFFFFF
            z = state
            z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & 0xFFFFFFFFFFFFFFFF
            z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & 0xFFFFFFFFFFFFFFFF
            table.append(z ^ (z >> 31))
        self._gear_hash_table = table
        return table

    def _cdc_chunks(self, data, min_size=2048, avg_size=8192, max_size=32768):
        chunks = []
        n = len(data)
        if n <= min_size:
            return [data] if n > 0 else []

        gear = self._gear_table()
        cursor = 0
        mask_bits = max(1, (avg_size - 1).bit_length() - 1)
        mask = (1 << mask_bits) - 1

        while cursor < n:
            end = cursor + min_size
            if end >= n:
                chunks.append(data[cursor:])
                break

            h = 0
            limit = min(cursor + max_size, n)
            found = False
            for i in range(end, limit):
                h = ((h << 1) + gear[data[i]]) & 0xFFFFFFFFFFFFFFFF
                if not (h & mask):
                    chunks.append(data[cursor:i+1])
                    cursor = i + 1
                    found = True
                    break

            if not found:
                chunks.append(data[cursor:limit])
                cursor = limit
        return chunks

    def _model_component_chunks(self, data):
        avg_size = self._env_int(
            "KOLIBRI_VAULT_META_AVG_CHUNK_SIZE",
            self._env_int("KOLIBRI_VAULT_META_CHUNK_SIZE", 8192),
        )
        method = os.environ.get("KOLIBRI_VAULT_CHUNK_METHOD", "cdc").lower()
        if method == "fixed":
            return [data[i:i + avg_size] for i in range(0, len(data), avg_size)] if data else []

        min_size = self._env_int("KOLIBRI_VAULT_CDC_MIN_SIZE", max(512, avg_size // 4))
        max_size = self._env_int("KOLIBRI_VAULT_CDC_MAX_SIZE", max(avg_size * 4, min_size))
        if min_size > avg_size:
            min_size = max(1, avg_size // 2)
        if max_size < avg_size:
            max_size = avg_size * 4
        return self._cdc_chunks(data, min_size=min_size, avg_size=avg_size, max_size=max_size)

    def _chunk_config(self):
        avg_size = self._env_int(
            "KOLIBRI_VAULT_META_AVG_CHUNK_SIZE",
            self._env_int("KOLIBRI_VAULT_META_CHUNK_SIZE", 8192),
        )
        method = os.environ.get("KOLIBRI_VAULT_CHUNK_METHOD", "cdc").lower()
        return {
            "method": method,
            "avg_size": avg_size,
            "min_size": self._env_int("KOLIBRI_VAULT_CDC_MIN_SIZE", max(512, avg_size // 4)),
            "max_size": self._env_int("KOLIBRI_VAULT_CDC_MAX_SIZE", avg_size * 4),
        }

    def _sha256(self, data):
        return hashlib.sha256(data).hexdigest()

    def _snapshot_bytes(self, snapshot):
        return sum(item["size"] for item in snapshot["files"].values())

    def _compare_manifest(self, manifest, restored):
        expected = {
            "files": manifest.get("files", {}),
            "dirs": set(manifest.get("dirs", [])),
            "symlinks": manifest.get("symlinks", {}),
            "bytes": int(manifest.get("bytes", 0) or 0),
        }
        return self.agi._compare_snapshots(expected, restored)

    def _manifest_from_snapshot(self, snapshot):
        return {
            "files": snapshot["files"],
            "dirs": sorted(snapshot["dirs"]),
            "symlinks": snapshot["symlinks"],
            "bytes": snapshot["bytes"],
        }

    def _compress_payload(self, payload):
        zdata = zlib.compress(payload, 9)
        if len(zdata) < len(payload):
            return "zlib", zdata
        return "store", payload

    def _decompress_payload(self, codec, payload):
        if codec == "store":
            return payload
        if codec == "zlib":
            return zlib.decompress(payload)
        raise ValueError(f"Unknown payload codec: {codec}")

    def _encode_model_meta_formula(self, components):
        chunk_config = self._chunk_config()
        chunks = []
        lookup = {}
        entries = {}
        raw_model_bytes = 0

        for name, data in sorted(components.items()):
            raw_model_bytes += len(data)
            indices = []
            
            for chunk in self._model_component_chunks(data):
                idx = lookup.get(chunk)
                if idx is None:
                    idx = len(chunks)
                    lookup[chunk] = idx
                    chunks.append(chunk)
                indices.append(idx)
            index_codec, index_payload = self.agi._encode_indices(indices)
            entries[name] = {
                "size": len(data),
                "sha256": self._sha256(data),
                "index_codec": index_codec,
                "indices": index_payload,
                "index_count": len(indices),
            }

        chunk_data = b"".join(chunks)
        length_codec, length_payload = self.agi._encode_blob(self.agi._pack_uvarints([len(c) for c in chunks]))
        chunk_data_codec, chunk_data_payload = self.agi._encode_blob(chunk_data)
        meta = {
            "type": "vault_model_meta_formula_v1",
            "formula": "component[name] = concat(chunk_table[i] for i in component_indices[name])",
            "chunk_method": chunk_config["method"],
            "chunk_size": chunk_config["avg_size"],
            "cdc_min_size": chunk_config["min_size"],
            "cdc_max_size": chunk_config["max_size"],
            "component_count": len(entries),
            "chunk_count": len(chunks),
            "raw_model_bytes": raw_model_bytes,
            "unique_chunk_bytes": len(chunk_data),
            "chunk_payload_bytes": len(chunk_data_payload),
            "index_bytes": sum(len(entry["indices"]) for entry in entries.values()),
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

    def _decode_model_meta_formula(self, meta):
        if meta.get("type") != "vault_model_meta_formula_v1":
            raise ValueError(f"Unknown model meta formula: {meta.get('type')}")
        count = int(meta.get("chunk_count", 0) or 0)
        lengths = self.agi._unpack_uvarints(
            self.agi._decode_blob(meta["chunk_length_codec"], meta["chunk_lengths"]),
            count,
        )
        chunk_data = self.agi._decode_blob(meta.get("chunk_data_codec", "store"), meta.get("chunk_data", b""))
        chunks = []
        cursor = 0
        for length in lengths:
            chunk = chunk_data[cursor:cursor + length]
            if len(chunk) != length:
                raise ValueError("Corrupt model meta formula: truncated chunk table")
            chunks.append(chunk)
            cursor += length
        if cursor != len(chunk_data):
            raise ValueError("Corrupt model meta formula: trailing chunk bytes")

        components = {}
        for name, entry in meta.get("entries", {}).items():
            indices = self.agi._decode_indices(entry["index_codec"], entry["indices"], int(entry["index_count"]))
            try:
                data = b"".join(chunks[i] for i in indices)
            except IndexError as exc:
                raise ValueError(f"Corrupt model meta formula: bad chunk index in {name}") from exc
            if len(data) != int(entry.get("size", 0) or 0):
                raise ValueError(f"Corrupt model meta formula: size mismatch in {name}")
            if self._sha256(data) != entry.get("sha256"):
                raise ValueError(f"Corrupt model meta formula: sha256 mismatch in {name}")
            components[name] = data
        return components

    def _encode_corpus(self, corpus):
        raw = pickle.dumps(corpus, protocol=pickle.HIGHEST_PROTOCOL)
        codec, payload = self._compress_payload(raw)
        return raw, codec, payload

    def _read_vault(self, vault_path):
        with open(vault_path, "rb") as f:
            magic = f.read(len(VAULT_MAGIC))
            if magic != VAULT_MAGIC:
                raise ValueError("Invalid Magic")
            meta_len_data = f.read(4)
            if len(meta_len_data) != 4:
                raise ValueError("Corrupt vault: missing metadata length")
            meta_len = struct.unpack(">I", meta_len_data)[0]
            meta_payload = f.read(meta_len)
            if len(meta_payload) != meta_len:
                raise ValueError("Corrupt vault: truncated metadata")
            metadata = pickle.loads(meta_payload)
            corpus_len_data = f.read(4)
            if len(corpus_len_data) != 4:
                raise ValueError("Corrupt vault: missing corpus length")
            corpus_len = struct.unpack(">I", corpus_len_data)[0]
            corpus_payload = f.read(corpus_len)
            if len(corpus_payload) != corpus_len:
                raise ValueError("Corrupt vault: truncated corpus")
            if f.read(1):
                raise ValueError("Corrupt vault: trailing bytes")

        if metadata.get("corpus_payload_sha256") and self._sha256(corpus_payload) != metadata["corpus_payload_sha256"]:
            raise ValueError("Corrupt vault: corpus payload sha256 mismatch")
        corpus_codec = metadata.get("corpus_codec", "zlib")
        raw_corpus = self._decompress_payload(corpus_codec, corpus_payload)
        if self._sha256(raw_corpus) != metadata.get("corpus_sha256"):
            raise ValueError("Corrupt vault: corpus sha256 mismatch")
        return metadata, pickle.loads(raw_corpus)

    def _components_from_corpus(self, corpus):
        if "model_meta_formula" in corpus:
            return self._decode_model_meta_formula(corpus["model_meta_formula"])

        components = {"map": corpus["map"]}
        for atom_hash, atom_data in corpus.get("atoms", {}).items():
            components[f"atom:{atom_hash}"] = atom_data
        for wm_hash, wm_data in corpus.get("world_model", {}).items():
            components[f"world_model:{wm_hash}"] = wm_data
        return components

    def _split_components(self, components):
        atoms = {}
        world_model = {}
        map_data = components.get("map")
        if map_data is None:
            raise ValueError("Vault model does not contain map component")
        for name, data in components.items():
            if name.startswith("atom:"):
                atoms[name.split(":", 1)[1]] = data
            elif name.startswith("world_model:"):
                world_model[name.split(":", 1)[1]] = data
        return map_data, atoms, world_model

    def _hologram_refs(self, atoms):
        refs = set()
        for atom_data in atoms.values():
            ref = self.agi._world_model_ref_from_atom(atom_data)
            if ref:
                refs.add(ref)
        return refs

    def _model_stats(self, components, model_meta=None):
        atom_count = sum(1 for name in components if name.startswith("atom:"))
        wm_count = sum(1 for name in components if name.startswith("world_model:"))
        raw_bytes = sum(len(data) for data in components.values())
        stats = {
            "components": len(components),
            "atoms": atom_count,
            "world_model": wm_count,
            "raw_model_bytes": raw_bytes,
            "meta_payload_bytes": 0,
            "chunk_payload_bytes": 0,
            "chunk_count": 0,
        }
        if model_meta:
            stats.update({
                "meta_payload_bytes": int(model_meta.get("meta_payload_bytes", 0) or 0),
                "chunk_payload_bytes": int(model_meta.get("chunk_payload_bytes", 0) or 0),
                "chunk_count": int(model_meta.get("chunk_count", 0) or 0),
                "chunk_method": model_meta.get("chunk_method", "unknown"),
            })
        return stats

    def create(self, source_path, vault_path, seed_name, mode="standalone", strategy="auto"):
        print(f"[*] Creating {mode.upper()} Vault: {vault_path}")
        start_time = time.time()
        source_path = os.path.abspath(os.path.expanduser(source_path))
        snapshot = self.agi._snapshot(source_path)

        self.agi.pack(source_path, seed_name)

        seed_data, seed_real_name = self.agi._read_seed(seed_name)
        map_hash = seed_data.hex()
        map_data, archive = self.agi._load_archive(map_hash)

        atom_hashes = set()
        for f_info in archive["files"].values():
            atom_hashes.add(f_info if isinstance(f_info, str) else f_info["block"])

        atoms = {}
        materialized_atoms = {}
        linked_refs = set()
        world_model_components = {}
        needs_embedded = mode == "standalone" and strategy in ("auto", "embedded_world_model")
        needs_materialized = mode == "standalone" and strategy in ("auto", "materialized_atoms")
        for atom_hash in sorted(atom_hashes):
            atom_path = os.path.join(self.agi.atoms_dir, atom_hash)
            with open(atom_path, "rb") as f:
                atom_data = f.read()
            atoms[atom_hash] = atom_data
            materialized_atoms[atom_hash] = atom_data
            wm_ref = self.agi._world_model_ref_from_atom(atom_data)
            if wm_ref:
                linked_refs.add(wm_ref)
                if mode == "standalone":
                    wm_path = os.path.join(self.agi.world_model_dir, wm_ref)
                    if not os.path.exists(wm_path):
                        raise RuntimeError(f"Standalone vault cannot be built: missing world model {wm_ref}")
                    wm_data = None
                    if needs_embedded or needs_materialized:
                        with open(wm_path, "rb") as f:
                            wm_data = f.read()
                    if needs_embedded:
                        world_model_components[wm_ref] = wm_data
                    if needs_materialized:
                        if wm_data.startswith(self.kolibri_agi.ATOM_FORMULA_MAGIC):
                            summary = self.agi._inspect_atom_formula(wm_data)
                            if summary.get("sha256") != wm_ref:
                                raise RuntimeError(f"World model formula hash mismatch: {wm_ref}")
                            materialized_atoms[atom_hash] = wm_data
                        else:
                            materialized_atoms[atom_hash] = self.agi._encode_atom_formula(wm_data, skip_hologram=True)

        manifest = self._manifest_from_snapshot(snapshot)

        candidate_inputs = []
        if mode == "standalone":
            if strategy in ("auto", "embedded_world_model"):
                embedded_components = {"map": map_data}
                embedded_components.update({f"atom:{atom_hash}": data for atom_hash, data in atoms.items()})
                embedded_components.update({f"world_model:{wm_hash}": data for wm_hash, data in world_model_components.items()})
                candidate_inputs.append(("embedded_world_model", embedded_components))

            if strategy in ("auto", "materialized_atoms"):
                materialized_components = {"map": map_data}
                materialized_components.update({f"atom:{atom_hash}": data for atom_hash, data in materialized_atoms.items()})
                candidate_inputs.append(("materialized_atoms", materialized_components))
        else:
            linked_components = {"map": map_data}
            linked_components.update({f"atom:{atom_hash}": data for atom_hash, data in atoms.items()})
            candidate_inputs.append(("linked_refs", linked_components))

        selected = None
        candidate_reports = []
        for strategy, components in candidate_inputs:
            model_meta = self._encode_model_meta_formula(components)
            model_stats = self._model_stats(components, model_meta)
            corpus = {
                "version": self.kolibri_agi.ARCHIVE_VERSION,
                "format": "kolibri.vault.model_meta_formula.v1",
                "mode": mode,
                "standalone_strategy": strategy if mode == "standalone" else "",
                "seed_name": seed_real_name,
                "map_hash": map_hash,
                "model_meta_formula": model_meta,
                "manifest": manifest,
                "linked_world_model_refs": sorted(linked_refs) if mode == "linked" else [],
                "timestamp": time.time(),
            }
            raw_corpus, corpus_codec, corpus_payload = self._encode_corpus(corpus)
            candidate = {
                "strategy": strategy,
                "components": components,
                "model_meta": model_meta,
                "corpus": corpus,
                "raw_corpus": raw_corpus,
                "corpus_codec": corpus_codec,
                "corpus_payload": corpus_payload,
                "model_stats": model_stats,
            }
            candidate_reports.append({
                "strategy": strategy,
                "corpus_payload_bytes": len(corpus_payload),
                "model_meta_payload_bytes": model_stats["meta_payload_bytes"],
                "raw_model_bytes": model_stats["raw_model_bytes"],
                "components": model_stats["components"],
            })
            if selected is None or len(corpus_payload) < len(selected["corpus_payload"]):
                selected = candidate

        components = selected["components"]
        model_meta = selected["model_meta"]
        corpus = selected["corpus"]
        raw_corpus = selected["raw_corpus"]
        corpus_codec = selected["corpus_codec"]
        corpus_payload = selected["corpus_payload"]
        metadata = {
            "version": "v2.1.model_meta",
            "mode": mode,
            "standalone_strategy": selected["strategy"] if mode == "standalone" else "",
            "seed": seed_data,
            "seed_name": seed_real_name,
            "raw_size": self._snapshot_bytes(snapshot),
            "files_count": len(snapshot["files"]),
            "dirs_count": len(snapshot["dirs"]),
            "symlinks_count": len(snapshot["symlinks"]),
            "corpus_codec": corpus_codec,
            "corpus_sha256": self._sha256(raw_corpus),
            "corpus_payload_sha256": self._sha256(corpus_payload),
            "corpus_raw_bytes": len(raw_corpus),
            "corpus_payload_bytes": len(corpus_payload),
            "model_stats": selected["model_stats"],
            "strategy_candidates": candidate_reports,
        }
        meta_data = pickle.dumps(metadata, protocol=pickle.HIGHEST_PROTOCOL)

        out_dir = os.path.dirname(os.path.abspath(os.path.expanduser(vault_path)))
        if out_dir:
            os.makedirs(out_dir, exist_ok=True)
        with open(vault_path, "wb") as f:
            f.write(VAULT_MAGIC)
            f.write(struct.pack(">I", len(meta_data)))
            f.write(meta_data)
            f.write(struct.pack(">I", len(corpus_payload)))
            f.write(corpus_payload)

        vault_size = os.path.getsize(vault_path)
        ratio = metadata["raw_size"] / vault_size if vault_size else 0
        duration = time.time() - start_time
        print(f"[+] Vault sealed: {vault_path} ({vault_size} bytes)")
        print(f"[*] Source bytes: {metadata['raw_size']}")
        print(f"[*] Files/dirs/symlinks: {metadata['files_count']}/{metadata['dirs_count']}/{metadata['symlinks_count']}")
        print(f"[*] Model formula components: {metadata['model_stats']['components']}")
        print(f"[*] Atoms/world-model: {metadata['model_stats']['atoms']}/{metadata['model_stats']['world_model']}")
        if mode == "standalone":
            print(f"[*] Standalone strategy: {metadata['standalone_strategy']}")
            for report in candidate_reports:
                print(
                    f"    - {report['strategy']}: corpus {report['corpus_payload_bytes']} bytes, "
                    f"model-meta {report['model_meta_payload_bytes']} bytes"
                )
        print(f"[*] Model raw/meta payload: {metadata['model_stats']['raw_model_bytes']}/{metadata['model_stats']['meta_payload_bytes']} bytes")
        print(f"[*] Chunk method: {metadata['model_stats'].get('chunk_method', 'unknown')}")
        print(f"[*] Model chunks: {metadata['model_stats']['chunk_count']}")
        print(f"[*] Corpus codec: {corpus_codec}")
        print(f"[*] Ratio: {ratio:.2f}x")
        print(f"[*] Duration: {duration:.2f}s")
        if mode == "linked":
            print(f"[*] Linked world-model refs: {len(linked_refs)}")

    def verify(self, vault_path):
        print(f"[*] Verifying Vault: {vault_path}")
        try:
            metadata, corpus = self._read_vault(vault_path)
            components = self._components_from_corpus(corpus)
            map_data, atoms, world_model = self._split_components(components)
            if self._sha256(map_data) != corpus["map_hash"]:
                raise ValueError("map hash mismatch")
            missing_refs = sorted(self._hologram_refs(atoms) - set(world_model))
            if corpus.get("mode") == "standalone" and missing_refs:
                raise ValueError(f"standalone vault misses world model refs: {len(missing_refs)}")
        except Exception as exc:
            print(f"[!] FAILED: {exc}")
            return False

        stats = self._model_stats(components, corpus.get("model_meta_formula"))
        print("[+] SUCCESS: Vault integrity verified.")
        print(f"[*] Version: {metadata.get('version')}")
        print(f"[*] Mode: {corpus.get('mode')}")
        if metadata.get("standalone_strategy"):
            print(f"[*] Standalone strategy: {metadata['standalone_strategy']}")
        print(f"[*] Seed: {metadata['seed'].hex()[:16]}...")
        print(f"[*] Files in manifest: {len(corpus['manifest']['files'])}")
        print(f"[*] Components: {stats['components']}")
        print(f"[*] Atoms/world-model: {stats['atoms']}/{stats['world_model']}")
        if corpus.get("mode") == "linked":
            print(f"[*] Linked world-model refs: {len(self._hologram_refs(atoms))}")
        print(f"[*] Model raw/meta payload: {stats['raw_model_bytes']}/{stats['meta_payload_bytes']} bytes")
        print(f"[*] Chunk method: {stats.get('chunk_method', 'unknown')}")
        return True

    def extract(self, vault_path, target_dir):
        metadata, corpus = self._read_vault(vault_path)
        components = self._components_from_corpus(corpus)
        map_data, atoms, world_model = self._split_components(components)

        if self._sha256(map_data) != corpus["map_hash"]:
            raise ValueError("Refusing to extract corrupted vault: map hash mismatch")
        missing_refs = sorted(self._hologram_refs(atoms) - set(world_model))
        if corpus.get("mode") == "standalone" and missing_refs:
            raise ValueError(f"Refusing to extract incomplete standalone vault: missing {len(missing_refs)} world model refs")
        if corpus.get("mode") == "linked" and missing_refs:
            unavailable = [
                ref for ref in missing_refs
                if not os.path.exists(os.path.join(self.agi.world_model_dir, ref))
            ]
            if unavailable:
                raise ValueError(f"Linked vault requires local World Model refs: missing {len(unavailable)}")

        map_path = os.path.join(self.agi.maps_dir, corpus["map_hash"])
        with open(map_path, "wb") as f:
            f.write(map_data)

        print("[*] Materializing model formula...")
        for atom_hash, atom_data in atoms.items():
            atom_path = os.path.join(self.agi.atoms_dir, atom_hash)
            with open(atom_path, "wb") as f:
                f.write(atom_data)
        for wm_hash, wm_data in world_model.items():
            wm_path = os.path.join(self.agi.world_model_dir, wm_hash)
            with open(wm_path, "wb") as f:
                f.write(wm_data)

        seed_path = os.path.join(self.agi.seeds_dir, f"vlt_{int(time.time())}.seed")
        with open(seed_path, "wb") as f:
            f.write(metadata["seed"])

        print("[*] Regenerating Project...")
        self.agi.unpack(seed_path, target_dir)
        if os.path.exists(seed_path):
            os.remove(seed_path)

        print("[*] Performing Bit-Exact Verification...")
        restored_snapshot = self.agi._snapshot(target_dir)
        errors = self._compare_manifest(corpus["manifest"], restored_snapshot)
        if errors:
            print("[!] BIT-EXACT MISMATCH")
            for err in errors[:20]:
                print(f"    - {err}")
            if len(errors) > 20:
                print(f"    ... {len(errors) - 20} more")
        else:
            print("[+] BIT-EXACT MATCH: Verified.")
        print(f"[*] Restored bytes: {restored_snapshot['bytes']}")
        print(f"[*] Files/dirs/symlinks: {len(restored_snapshot['files'])}/{len(restored_snapshot['dirs'])}/{len(restored_snapshot['symlinks'])}")


def main():
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command")

    p_create = subparsers.add_parser("create")
    p_create.add_argument("source")
    p_create.add_argument("vault")
    p_create.add_argument("--seed", required=True)
    p_create.add_argument("--mode", choices=["standalone", "linked"], default="standalone")
    p_create.add_argument("--strategy", choices=["auto", "embedded_world_model", "materialized_atoms"], default="auto")

    p_extract = subparsers.add_parser("extract")
    p_extract.add_argument("vault")
    p_extract.add_argument("target")

    p_verify = subparsers.add_parser("verify")
    p_verify.add_argument("vault")

    args = parser.parse_args()
    vault = KolibriVault()

    if args.command == "create":
        vault.create(args.source, args.vault, args.seed, args.mode, args.strategy)
    elif args.command == "extract":
        vault.extract(args.vault, args.target)
    elif args.command == "verify":
        vault.verify(args.vault)
    else:
        parser.print_help()


if __name__ == "__main__":
    main()
