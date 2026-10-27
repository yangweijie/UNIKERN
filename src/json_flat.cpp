#include "json_flat.hpp"

namespace unikern::detail {

std::string trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return {};
  size_t b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}

bool json_has(const std::string& j, const std::string& key) {
  return j.find("\"" + key + "\"") != std::string::npos;
}

std::optional<std::string> json_field(const std::string& j, const std::string& key) {
  const std::string pat = "\"" + key + "\"";
  size_t pos = 0;
  while ((pos = j.find(pat, pos)) != std::string::npos) {
    size_t p = j.find(':', pos + pat.size());
    if (p == std::string::npos) return std::nullopt;
    ++p;
    while (p < j.size() && (j[p] == ' ' || j[p] == '\t')) ++p;
    if (p >= j.size()) return std::nullopt;
    if (j[p] == '"') {
      size_t e = j.find('"', p + 1);
      if (e == std::string::npos) return std::nullopt;
      return j.substr(p + 1, e - p - 1);
    }
    size_t e = j.find_first_of(",}\n", p);
    if (e == std::string::npos) e = j.size();
    return trim(j.substr(p, e - p));
  }
  return std::nullopt;
}

std::vector<std::string> json_objects(const std::string& j, const std::string& key) {
  std::vector<std::string> out;
  const std::string pat = "\"" + key + "\"";
  const size_t k = j.find(pat);
  if (k == std::string::npos) return out;
  const size_t open = j.find('[', k + pat.size());
  if (open == std::string::npos) return out;
  int depth = 0;
  std::string cur;
  for (size_t p = open + 1; p < j.size(); ++p) {
    const char c = j[p];
    if (c == '{') {
      if (depth == 0) cur.clear();
      ++depth;
    }
    if (depth > 0) cur.push_back(c);
    if (c == '}') {
      --depth;
      if (depth == 0) out.push_back(cur);
    }
    if (c == ']' && depth == 0) break;  // array end
  }
  return out;
}

std::vector<std::string> json_numbers(const std::string& j, const std::string& key) {
  std::vector<std::string> out;
  const std::string pat = "\"" + key + "\"";
  const size_t k = j.find(pat);
  if (k == std::string::npos) return out;
  const size_t open = j.find('[', k + pat.size());
  if (open == std::string::npos) return out;
  const size_t close = j.find(']', open);
  if (close == std::string::npos) return out;
  size_t p = open + 1;
  while (p < close) {
    size_t comma = j.find(',', p);
    if (comma == std::string::npos || comma > close) comma = close;
    const std::string tok = trim(j.substr(p, comma - p));
    if (!tok.empty()) out.push_back(tok);
    p = comma + 1;
  }
  return out;
}

std::vector<std::string> json_keys(const std::string& j) {
  std::vector<std::string> out;
  int depth = 0;
  bool in_str = false, esc = false, capturing = false;
  std::string cur;
  for (size_t p = 0; p < j.size(); ++p) {
    const char c = j[p];
    if (in_str) {
      if (esc) { esc = false; cur.push_back(c); continue; }
      if (c == '\\') { esc = true; cur.push_back(c); continue; }
      if (c == '"') {
        in_str = false;
        if (capturing && depth == 1) {
          size_t q = p + 1;
          while (q < j.size() && (j[q] == ' ' || j[q] == '\t' || j[q] == '\n' ||
                                  j[q] == '\r'))
            ++q;
          if (q < j.size() && j[q] == ':') out.push_back(cur);
        }
        capturing = false;
        continue;
      }
      cur.push_back(c);
      continue;
    }
    if (c == '"') { in_str = true; capturing = true; cur.clear(); continue; }
    if (c == '{' || c == '[') { ++depth; continue; }
    if (c == '}' || c == ']') { --depth; continue; }
  }
  return out;
}

}  // namespace unikern::detail
