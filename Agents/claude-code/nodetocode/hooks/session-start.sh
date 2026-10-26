#!/usr/bin/env bash
# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
#
# SessionStart hook of the nodetocode Claude Code plugin: says in one line which route reaches NodeToCode's MCP tools.
#
# It probes, in parallel and with short timeouts (well under 2 seconds with no editor running, Windows included):
#   - the plugin's MCP URL (the plugin option mcp_url, default http://127.0.0.1:27000/mcp), at <url>/health;
#   - every running editor's NodeToCode server from its discovery entry (<user settings dir>/NodeToCode/servers/
#     <pid>.json, and <project>/Saved/NodeToCode/mcp-server.json), the same files the stdio bridge reads;
#   - Epic's Unreal MCP (UE 5.8) at http://127.0.0.1:8000/mcp, or NODETOCODE_UNREAL_MCP_URL.
# It prints one JSON object: systemMessage is the line the user sees, additionalContext tells Claude the same.
# It always exits 0 and never writes anything. Set NODETOCODE_HOOK_DEBUG to log to stderr.

debug() { [ -n "$NODETOCODE_HOOK_DEBUG" ] && echo "nodetocode session-start: $*" >&2; }

default_url="http://127.0.0.1:27000/mcp"
url="${CLAUDE_PLUGIN_OPTION_MCP_URL:-$default_url}"
url="${url%/}"
unreal_url="${NODETOCODE_UNREAL_MCP_URL:-http://127.0.0.1:8000/mcp}"
server_name="plugin:nodetocode:nodetocode"
tool_prefix="mcp__plugin_nodetocode_nodetocode__"

json_escape() {
  local s="$1"
  s="${s//\\/\\\\}"
  s="${s//\"/\\\"}"
  s="${s//$'\t'/ }"
  s="${s//$'\r'/}"
  s="${s//$'\n'/ }"
  printf '%s' "$s"
}

emit() { # $1 = line for the user, $2 = context for Claude
  printf '{"systemMessage":"%s","hookSpecificOutput":{"hookEventName":"SessionStart","additionalContext":"%s"}}\n' \
    "$(json_escape "$1")" "$(json_escape "$2")"
  exit 0
}

if ! command -v curl >/dev/null 2>&1; then
  emit "NodeToCode: curl is not on PATH, so the editor could not be checked; /mcp shows whether $server_name connected." \
       "The nodetocode plugin could not check NodeToCode's MCP server (no curl). Its tools are named ${tool_prefix}<tool>."
fi

# --- Discovery entries ------------------------------------------------------------------------------------------
to_posix() { # a Windows path to one bash can open (Git Bash, MSYS, Cygwin); other paths unchanged
  if command -v cygpath >/dev/null 2>&1; then cygpath -u "$1" 2>/dev/null || printf '%s' "$1"; else printf '%s' "$1"; fi
}

if [ -n "$NODETOCODE_DISCOVERY_DIR" ]; then
  index_dir="$(to_posix "$NODETOCODE_DISCOVERY_DIR")"
elif [ -n "$LOCALAPPDATA" ]; then
  index_dir="$(to_posix "$LOCALAPPDATA")/NodeToCode/servers"
elif [ "$(uname -s 2>/dev/null)" = "Darwin" ]; then
  index_dir="$HOME/Library/Application Support/Epic/NodeToCode/servers"
else
  index_dir="$HOME/.config/Epic/NodeToCode/servers"
fi
project_dir="$(to_posix "${CLAUDE_PROJECT_DIR:-$PWD}")"
project_entry="$project_dir/Saved/NodeToCode/mcp-server.json"

field() { # $1 = key, $2 = a discovery entry (FN2CMcpHttpServerManager::WriteDiscoveryFiles, pretty-printed) on one line
  printf '%s' "$2" | sed -n "s/.*\"$1\"[[:space:]]*:[[:space:]]*\"\\([^\"]*\\)\".*/\\1/p" | head -n 1
}

entry_urls=()
entry_labels=()
entry_is_project=()
add_entry() { # $1 = file, $2 = 1 when it is this project's own entry
  local text u name engine i
  text="$(tr -d '\r\n' < "$1" 2>/dev/null)" || return
  u="$(field url "$text")"
  [ -n "$u" ] || return
  u="${u%/}"
  for i in "${!entry_urls[@]}"; do
    if [ "${entry_urls[$i]}" = "$u" ]; then
      [ "$2" = 1 ] && entry_is_project[$i]=1
      return
    fi
  done
  name="$(field projectName "$text")"
  engine="$(field engineVersion "$text")"
  entry_urls+=("$u")
  entry_labels+=("${name:-an editor}${engine:+, UE ${engine%%-*}}")
  entry_is_project+=("$2")
}
[ -f "$project_entry" ] && add_entry "$project_entry" 1
if [ -d "$index_dir" ]; then
  count=0
  for f in "$index_dir"/*.json; do
    [ -f "$f" ] || continue
    count=$((count + 1))
    [ "$count" -le 8 ] || break
    add_entry "$f" 0
  done
fi
debug "url=$url index=$index_dir entries=${entry_urls[*]}"

# --- Probes, all at once ----------------------------------------------------------------------------------------
tmp="$(mktemp -d 2>/dev/null || echo "${TMPDIR:-/tmp}/nodetocode-hook-$$")"
mkdir -p "$tmp" 2>/dev/null
trap 'rm -rf "$tmp"' EXIT

probe_health() { # $1 = MCP URL, $2 = output file: written only when NodeToCode's health check answers
  local body
  body="$(curl -s --noproxy '*' --connect-timeout 0.5 --max-time 1.5 "$1/health" 2>/dev/null)"
  case "$body" in *NodeToCode*) printf 'ok' > "$2" ;; esac
}
probe_any() { # $1 = URL, $2 = output file: written when anything answers HTTP there
  local code
  code="$(curl -s -o /dev/null -w '%{http_code}' --noproxy '*' --connect-timeout 0.5 --max-time 1.0 \
          -H 'Accept: text/event-stream' "$1" 2>/dev/null)"
  case "$code" in ''|000) ;; *) printf '%s' "$code" > "$2" ;; esac
}

probe_health "$url" "$tmp/main" &
for i in "${!entry_urls[@]}"; do
  [ "${entry_urls[$i]}" = "$url" ] || probe_health "${entry_urls[$i]}" "$tmp/entry$i" &
done
probe_any "$unreal_url" "$tmp/unreal" &
wait

unreal_note=""
[ -f "$tmp/unreal" ] && unreal_note=" Epic's Unreal MCP also answers at $unreal_url (UE 5.8: NodeToCode's tools are reachable there through call_tool with toolset_name NodeToCode when the MCP Client Toolset mounts it)."

if [ -f "$tmp/main" ]; then
  label=""
  for i in "${!entry_urls[@]}"; do [ "${entry_urls[$i]}" = "$url" ] && label=" (${entry_labels[$i]})"; done
  emit "NodeToCode: the editor's MCP server answers at $url$label; use the $server_name tools." \
       "NodeToCode's MCP server answers at $url$label. Reach NodeToCode through the plugin's MCP server: its tools are named ${tool_prefix}<tool> (for example ${tool_prefix}get-blueprint-n2cjson). Prefer it over Epic's Unreal MCP route, which cuts calls off after about 30 seconds.$unreal_note"
fi

found=""
for i in "${!entry_urls[@]}"; do
  if [ -f "$tmp/entry$i" ]; then
    if [ -z "$found" ] || [ "${entry_is_project[$i]}" = 1 ]; then found="$i"; fi
  fi
done
if [ -n "$found" ]; then
  other="${entry_urls[$found]}"
  emit "NodeToCode: nothing at $url, but ${entry_labels[$found]} serves NodeToCode at $other: set the plugin's mcp_url to it (/plugin > nodetocode > Configure, then /mcp reconnect) or use the stdio bridge (plugin README)." \
       "The nodetocode plugin's MCP server ($server_name) points at $url, where nothing answers, so its tools are missing. NodeToCode's server for ${entry_labels[$found]} answers at $other. Tell the user to set the plugin option mcp_url to $other and reconnect, or to use the stdio bridge.$unreal_note"
fi

if [ -f "$tmp/unreal" ]; then
  emit "NodeToCode: no NodeToCode server answers (checked $url); Epic's Unreal MCP answers at $unreal_url, but its NodeToCode toolset needs NodeToCode's server on too." \
       "No NodeToCode MCP server answers at $url or in any discovery entry, so NodeToCode's tools are unavailable, also through Epic's Unreal MCP (which only mounts a running NodeToCode server). The editor needs Project Settings > Plugins > Node to Code (User) > Enable MCP Server."
fi

emit "NodeToCode: no editor is serving NodeToCode's MCP server (checked $url). Open the project with NodeToCode's MCP server on, then run /mcp reconnect." \
     "No NodeToCode MCP server is running (checked $url and the editors' discovery entries), so the ${tool_prefix}* tools are unavailable. If the user asks for Blueprint work, tell them to open the project in Unreal Editor with Project Settings > Plugins > Node to Code (User) > Enable MCP Server on, then reconnect the $server_name server in /mcp. Do not guess at Blueprint contents."
