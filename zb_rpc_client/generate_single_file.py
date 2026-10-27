#!/usr/bin/env python3
"""Generate a single-file (header-only) version of the zb_rpc client.

The generated header contains the RPC engine headers, the client's C support
files (serial + PPP) and the lwIP sources they need.  main.cpp can then be
compiled as a single translation unit:

    g++ -std=c++26 -freflection -fpermissive -w -pthread \
        -DSINGLE_FILE_COMPILATION main.cpp -o zb_rpc_client

The C sources are compiled as C++ here; -fpermissive is needed for the C
implicit void* conversions.  Includes of project headers are replaced by the
header contents; system includes are kept as-is.  The generated header has no
dependency on the repository or on an lwIP checkout.
"""

from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import re

CLIENT_SOURCES = [
    "ppp_link.c",
    "ppp_lwip.c",
    "serial_io.c",
    "netdev_fwd.c",
    "ip6_stub.c",
]

# Headers used directly by main.cpp that are not reachable from the C sources.
EXTRA_HEADERS = [
    "zb_rpc/engine.hpp",
    "zb_rpc/log.hpp",
]

# ip4.c is renamed in the CMake build via -D ip4_input=lwip_ip4_input because
# ppp_lwip.c provides its own ip4_input().  Apply the same rename locally.
# ip4_frag.c and ip6_frag.c both define reassdatagrams (file-scope static);
# they are separate translation units in the normal build, but not here.
PER_FILE_RENAMES = {
    "ip4.c": {"ip4_input": "lwip_ip4_input"},
    "ip6_frag.c": {"reassdatagrams": "ip6_reassdatagrams"},
}

# Headers without include guards that are intentionally included multiple
# times with different helper macros (lwIP's memory pool table).
REPEATABLE_HEADERS = {"memp_std.h"}

INCLUDE_RE = re.compile(r'^[ \t]*#[ \t]*include[ \t]*([<"])([^>"]+)[>"]')
PRAGMA_ONCE_RE = re.compile(r'^[ \t]*#[ \t]*pragma[ \t]+once[ \t]*$', re.MULTILINE)


def find_lwip_dir(explicit: str | None) -> Path:
    candidates: list[Path] = []
    if explicit:
        candidates.append(Path(explicit))
    if os.environ.get("LWIP_DIR"):
        candidates.append(Path(os.environ["LWIP_DIR"]))
    # Adjacent checkout (this repository's README uses ../lwip) and the
    # location used during development of this script.
    here = Path(__file__).resolve().parent
    candidates += [
        here.parent / "lwip",
        here.parent.parent / "lwip",
        Path.home() / "repos" / "lwip",
    ]
    for candidate in candidates:
        if (candidate / "src" / "Filelists.cmake").is_file():
            return candidate.resolve()
    raise SystemExit(
        "lwIP sources not found. Set LWIP_DIR or pass --lwip-dir "
        "(expected <dir>/src/Filelists.cmake)."
    )


def lwip_sources(lwip_dir: Path) -> list[Path]:
    """Replicate the source selection from zb_rpc_client/CMakeLists.txt."""
    filelists = (lwip_dir / "src" / "Filelists.cmake").read_text()

    def sources_of(name: str) -> list[str]:
        match = re.search(r"set\(" + re.escape(name) + r"\s*(.*?)\)\s*\n", filelists, re.S)
        if not match:
            raise SystemExit(f"could not find {name} in Filelists.cmake")
        return re.findall(r"\$\{LWIP_DIR\}/(\S+)", match.group(1))

    groups = [
        "lwipcore_SRCS",
        "lwipcore4_SRCS",
        "lwipcore6_SRCS",
        "lwipnetif_SRCS",
        "lwipsixlowpan_SRCS",
        "lwipppp_SRCS",
    ]
    files = [f for group in groups for f in sources_of(group)]

    # The POSIX socket/netconn API is not used by this client.  Its headers
    # define struct in_addr/sockaddr_in/addrinfo, which clash with the host
    # libc headers in a single translation unit.  Keep tcpip.c (used by the
    # netdev forwarding path) and err.c, drop the rest.
    api = sources_of("lwipapi_SRCS")
    files += [f for f in api if f.endswith("/tcpip.c") or f.endswith("/err.c")]

    def is_one_of(path: str, *names: str) -> bool:
        return any(path.endswith("/" + name) for name in names)

    # CMakeLists.txt removes these and adds only ip4.c back.  inet6.c only
    # provides in6addr_any, which libc already provides on the host.
    files = [
        f
        for f in files
        if not is_one_of(f, "slipif.c", "ip4.c", "ip6.c", "inet6.c")
    ]
    files.append("src/core/ipv4/ip4.c")
    paths = [(lwip_dir / f).resolve() for f in files]
    paths.append((lwip_dir / "contrib" / "ports" / "unix" / "port" / "sys_arch.c").resolve())
    return paths


class Amalgamator:
    def __init__(self, roots: list[Path]):
        self.roots = [r.resolve() for r in roots]
        self.include_dirs = list(self.roots)
        self.emitted: set[Path] = set()
        self.stack: list[Path] = []

    def project_header(self, name: str, current_dir: Path) -> Path | None:
        candidates = [current_dir / name]
        candidates += [root / name for root in self.include_dirs]
        for candidate in candidates:
            try:
                resolved = candidate.resolve()
            except OSError:
                continue
            if resolved.is_file() and self.is_project_file(resolved):
                return resolved
        return None

    def is_project_file(self, path: Path) -> bool:
        return any(path == root or root in path.parents for root in self.roots)

    def direct_includes(self, path: Path) -> list[Path]:
        headers: list[Path] = []
        for line in path.read_text(errors="surrogateescape").splitlines():
            match = INCLUDE_RE.match(line)
            if not match:
                continue
            header = self.project_header(match.group(2), path.parent)
            if header is not None and header.name not in REPEATABLE_HEADERS:
                headers.append(header)
        return headers

    def expand_file(self, path: Path) -> str:
        resolved = path.resolve()
        repeatable = resolved.name in REPEATABLE_HEADERS
        if not repeatable and resolved in self.emitted:
            return ""
        if resolved in self.stack:
            # Include cycle: the original include guard makes this a no-op.
            return f"/* amalgam: recursive include of {path.name} skipped */\n"

        self.stack.append(resolved)
        try:
            text = resolved.read_text(errors="surrogateescape")
            if resolved.suffix in {".c", ".cpp", ".cc"}:
                for old, new in PER_FILE_RENAMES.get(resolved.name, {}).items():
                    text = re.sub(r"\b" + re.escape(old) + r"\b", new, text)

            guard = None
            if PRAGMA_ONCE_RE.search(text):
                digest = hashlib.sha1(str(resolved).encode()).hexdigest()[:16]
                guard = "ZB_AMALGAM_ONCE_" + digest
                text = PRAGMA_ONCE_RE.sub(
                    f"#ifndef {guard}\n#define {guard}", text, count=1
                )
                text += f"\n#endif /* {guard} */\n"

            output: list[str] = []
            for line in text.splitlines(keepends=True):
                match = INCLUDE_RE.match(line)
                if match:
                    header = self.project_header(match.group(2), resolved.parent)
                    if header is not None:
                        output.append(
                            f"/* amalgam: {match.group(2)} */\n"
                            + self.expand_file(header)
                        )
                        continue
                output.append(line)

            result = "".join(output)
            if not repeatable:
                self.emitted.add(resolved)
            return result
        finally:
            self.stack.pop()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--lwip-dir",
        help="lwIP source tree (default: $LWIP_DIR or a sibling ../lwip)",
    )
    parser.add_argument(
        "--output",
        default=str(Path(__file__).resolve().parent / "header_zb_rpc_client.hpp"),
        help="generated header path",
    )
    args = parser.parse_args()

    client_dir = Path(__file__).resolve().parent
    repo_dir = client_dir.parent
    lwip_dir = find_lwip_dir(args.lwip_dir)

    roots = [
        client_dir,
        client_dir / "include",
        repo_dir / "zb_rpc_shared" / "include",
        lwip_dir / "src" / "include",
        lwip_dir / "contrib" / "ports" / "unix" / "port" / "include",
    ]
    sources = [client_dir / name for name in CLIENT_SOURCES]
    sources += lwip_sources(lwip_dir)
    extra_headers = [repo_dir / "zb_rpc_shared" / "include" / name for name in EXTRA_HEADERS]

    amalgamator = Amalgamator(roots)

    # All project headers go into one preamble, before any source body.  This
    # means a header is emitted once even if its first #include happened to be
    # inside an inactive #if branch (lwIP has several "#if 0 /* UNUSED */"
    # include blocks), and keeps source bodies free of nested headers.
    preamble: list[str] = []
    for path in extra_headers:
        preamble.append(f"\n/* ===== {path.name} ===== */\n")
        preamble.append(amalgamator.expand_file(path))
    for source in sources:
        for header in amalgamator.direct_includes(source):
            preamble.append(f"\n/* ===== {header.name} ===== */\n")
            preamble.append(amalgamator.expand_file(header))

    body: list[str] = []
    for source in sources:
        body.append(f"\n/* ===== {source.name} ===== */\n")
        body.append(amalgamator.expand_file(source))

    banner = f"""\
/* Generated by zb_rpc_client/generate_single_file.py -- do not edit.
 *
 * Single-file build of the zb_rpc client.  Compile with:
 *   g++ -std=c++26 -freflection -fpermissive -w -pthread \\
 *       -DSINGLE_FILE_COMPILATION main.cpp -o zb_rpc_client
 *
 * lwIP: {lwip_dir}
 */
#ifndef HEADER_ZB_RPC_CLIENT_HPP
#define HEADER_ZB_RPC_CLIENT_HPP

#ifdef __cplusplus

/* The host libc owns the POSIX socket/netdb types; lwIP only needs its own
 * internal IP types here.  This keeps the two sets of declarations from
 * colliding in one translation unit. */
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#ifndef LWIP_SOCKET_EXTERNAL_HEADERS
#define LWIP_SOCKET_EXTERNAL_HEADERS 1
#endif
#ifndef LWIP_SOCKET_EXTERNAL_HEADER_INET_H
#define LWIP_SOCKET_EXTERNAL_HEADER_INET_H <netinet/in.h>
#endif
#ifndef LWIP_SOCKET_EXTERNAL_HEADER_SOCKETS_H
#define LWIP_SOCKET_EXTERNAL_HEADER_SOCKETS_H <sys/socket.h>
#endif
/* Only the unused POSIX compatibility aliases are disabled. */
#ifndef LWIP_COMPAT_SOCKETS
#define LWIP_COMPAT_SOCKETS 0
#endif
#define LWIP_DNS_API_DECLARE_STRUCTS 0
#define LWIP_DNS_API_DEFINE_ERRORS 0
#define LWIP_DNS_API_DEFINE_FLAGS 0
#define LWIP_DNS_API_DECLARE_H_ERRNO 0
"""

    output = (
        banner
        + "".join(preamble)
        + "".join(body)
        + "\n#endif /* __cplusplus */\n#endif /* HEADER_ZB_RPC_CLIENT_HPP */\n"
    )
    output_path = Path(args.output)
    output_path.write_text(output)
    print(f"wrote {output_path} ({len(output):,} bytes, {output.count(chr(10)):,} lines)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
