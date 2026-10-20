#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Explicit Codex CLI JSON adapter. Network/model invocation is opt-in at the generator boundary.

By default this adapter accepts only explicitly labeled repository-owned synthetic analysis.
Private executable analysis requires the additional --allow-private-analysis authorization.
The CLI/auth installation is trusted operator configuration, never generated code.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import implementation_generator as generator


def response_schema() -> dict:
    signature = {"type": "object", "additionalProperties": False,
                 "properties": {"symbol": {"type": "string"}, "calling_convention": {"type": "string"},
                                "returns": {"type": "string"},
                                "arguments": {"type": "array", "items": {"type": "string"}}},
                 "required": ["symbol", "calling_convention", "returns", "arguments"]}
    properties = {"analysis_sha256": {"type": "string"}, "signature": signature, "source": {"type": "string"},
                  "assumptions": {"type": "array", "items": {"type": "string"}},
                  "evidence": {"type": "array", "items": {"type": "string"}}}
    return {"type": "object", "additionalProperties": False, "properties": properties, "required": list(properties)}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--codex", type=Path, required=True)
    parser.add_argument("--model", required=True)
    parser.add_argument("--auth-directory", type=Path, required=True, help="existing operator-managed Codex auth directory")
    parser.add_argument("--allow-private-analysis", action="store_true")
    args = parser.parse_args(argv)
    try:
        request = json.load(sys.stdin)
        package = request["analysis"]
        if request.get("analysis_sha256") != generator.digest(package):
            raise generator.GenerationError("analysis package identity differs")
        if package.get("evidence_origin") != "project_owned_synthetic" and not args.allow_private_analysis:
            raise generator.GenerationError("private executable analysis requires separate explicit provider authorization")
        if not args.codex.is_absolute() or not args.codex.is_file() or not args.auth_directory.is_dir():
            raise generator.GenerationError("configured CLI/auth installation is unavailable")
        directory = Path.cwd()
        schema = directory / "provider-response-schema.json"
        response = directory / "provider-model-response.json"
        generator.pipeline.atomic_json(schema, response_schema())
        prompt = ("Reconstruct only this bounded function from the supplied evidence. Do not use tools, browse, read other files, "
                  "change any files or run commands. Return the requested JSON response. The source must be independent, readable "
                  "C++ with extern \"C\" linkage and the exact supplied signature. Preserve integer widths and machine shift masks. "
                  "Do not embed assembly, bytes or decompiler listings. No main, plugin initializer, attributes or global constructors. "
                  "State assumptions without admitting an ABI or claiming verification. Use only supplied evidence references.\n"
                  + json.dumps(request, sort_keys=True))
        command = [str(args.codex), "exec", "--ignore-user-config", "--sandbox", "read-only", "--ephemeral",
                   "--skip-git-repo-check", "--model", args.model, "--output-schema", str(schema),
                   "--output-last-message", str(response), "-"]
        # Auth location is explicitly chosen by the operator; no auth file is read or logged here.
        generator.bounded_process(command, directory, 240, prompt.encode(),
                                  environment={"CODEX_HOME": str(args.auth_directory),
                                               "XDG_RUNTIME_DIR": os.environ.get("XDG_RUNTIME_DIR", "")})
        result = generator.differential.read_json(response)
        generator.validate_response(result, package)
        json.dump(result, sys.stdout)
        return 0
    except (ValueError, OSError, KeyError, generator.subprocess.TimeoutExpired) as error:
        print(f"provider blocked: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
