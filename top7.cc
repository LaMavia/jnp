#include <iostream>
#include <ostream>
#include <ranges>
#include <regex>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

using namespace std;

/**
 * hipotezy:
 * 1. nie przynależenie do 7-ki nie wpływa na wyrzucanie
 * 2. wypada, jeśli kiedykolwiek dostało głos, a nie dostało w ostatnim. (p ->
 * q)
 *
 * dict[int,
 *      (~głosy_w_obecnym: int,
 *       ~pozycja_w_poprzednim: int [0: nigdy nie głosowane]
 *      )
 *     ]
 *
 */

namespace cached {

template <typename T> using cached = pair<bool, T>;

template <typename T> auto val(cached<T> &trunk) -> T & { return trunk.second; }

template <typename T> auto has_expired(cached<T> &trunk) -> bool {
  return trunk.first;
}

template <typename T> auto set(cached<T> &trunk, T &&new_value) -> cached<T> {
  trunk.second = new_value;
  trunk.first = true;

  return trunk;
}

template <typename T> auto expire(cached<T> &trunk) -> void {
  trunk.first = false;
}

} // namespace cached

namespace comparable {
template <typename K, typename V>
using comparable = pair<unordered_map<K, V>, unordered_map<K, V>>;

template <typename K, typename V>
auto old(comparable<K, V> &comp) -> unordered_map<K, V> {
  return comp.first;
}

template <typename K, typename V>
auto current(comparable<K, V> &comp) -> unordered_map<K, V> & {
  return comp.second;
}

template <typename K, typename V>
auto is(comparable<K, V> &comp, const K &key) -> bool {
  return comp.second.contains(key);
}

template <typename K, typename V>
auto was(comparable<K, V> &comp, const K &key) -> bool {
  return comp.first.contains(key);
}

template <typename K, typename V, typename U = V>
auto cmp(comparable<K, V> &comp, V default_value, function<U(K, V, V)> &&f)
    -> unordered_map<K, U> {
  unordered_map<K, U> result{};

  for (auto const &[key, prev_value] : old(comp)) {
    result.insert(key, f(key, prev_value,
                         is(comp, key) ? current(comp)[key] : default_value));
  }

  for (auto const &[key, current_value] : current(comp)) {
    result.insert(key, f(key, was(comp, key) ? old(comp)[key] : default_value,
                         current_value));
  }

  return result;
}

template <typename K, typename V>
auto map(comparable<K, V> &comp, function<void(unordered_map<K, V> &)> &f,
         function<void(unordered_map<K, V> &)> &g) -> comparable<K, V> & {
  f(old(comp));
  g(current(comp));

  return comp;
}

template <typename K, typename V>
auto shift(comparable<K, V> &comp, unordered_map<K, V> new_new)
    -> comparable<K, V> & {
  comp.first = comp.second;
  comp.second = new_new;

  return comp;
}
} // namespace comparable

using point_counter = unordered_map<uint64_t, uint64_t>;   // (1)
using placing = vector<uint64_t>;                          // (2)
using comparison_result = vector<pair<uint64_t, int64_t>>; // (3)

enum instruction_type { top = 1, max = 2, vote = 3, empty = 4, unknown = 0 };

/*
  1. something to hold the votes in each round:
    - quick song access
    - votes ≥ 0
    :: unordered_map<song_id, uint64_t>
  2. something for rounding up each round:
    - set order
    - size ≤ 7
    - constructable from (1)
    - access time complexity doesn't matter,
      since size ≤ 7, so O(n^2) = O(49) = O(1) is fine
    :: vector<song_id> ?
      - position = index + 1 & points = 7 - index
  3. something to compare the round ups:
    - set order, sortability would be nice
    - printable
    :: vector<pair<song_id, ~d_pos: int64_t>> # -7 < d_pos < 7, so '-' ~ 7
  4. something to store running points
    >> wild guess:
      za placement w podsumowaniu rundy dostajemy punkty.
      te punkty są zliczane w (1). Z tego robimy identycznie
      (2). Trzymamy poprzedni (2) i obecny (2) [tj. (2) z (1)
      przed dodaniem i po dodaniu punktów z tej rundy _zakończonej_].
      Na podstawie tego wylicza (3) do pokazania.
*/

auto instruction_type_of_line(string &line) -> instruction_type {
  const static map<instruction_type, regex> cases{
      {instruction_type::max, regex(R"(^MAX\s+\d+)")},
      {instruction_type::top, regex(R"(^TOP$)")},
      {instruction_type::vote, regex(R"(^([1-9]\d*\s*)+$)")},
      {instruction_type::empty, regex(R"(^\s*$)")}};

  for (auto const &[instruction, re] : cases) {
    if (regex_match(line, re)) {
      return instruction;
    }
  }

  return instruction_type::unknown;
}

// Parsers

auto parseVote(point_counter &currentVotes, size_t max_key, string &line)
    -> pair<bool, unordered_set<uint64_t>> {
  unordered_set<uint64_t> votes{};
  stringstream lineStream(line);
  bool valid = true;

  uint64_t vote;

  while (lineStream >> vote) {
    if (!currentVotes.contains(vote) || vote == 0 || vote > max_key ||
        votes.contains(vote)) {
      valid = false;
      break;
    }

    votes.insert(vote);
  }

  return {valid, votes};
}

auto parse_max(uint64_t max_key, string &line) -> pair<bool, uint64_t> {
  stringstream lineStream(line);
  bool valid = true;

  size_t new_max_key;

  // ignore leading whitespaces
  while (isspace(lineStream.peek())) {
    lineStream.ignore(1);
  }

  // ignore "MAX "
  lineStream.ignore(((sizeof(char)) * 4), ' ');

  valid = !!(lineStream >> new_max_key) && (new_max_key >= max_key);

  return {valid, new_max_key};
}

// end: parsers

// start: printers

auto printLineError(string &line, size_t lineNumber) -> void {
  cerr << "Error in line " << lineNumber << ": " << line << "\n";
}

// end: printers

auto main() -> int {
  string line;
  size_t max_key = 0;
  size_t line_number = 0;

  point_counter current_round_votes{};
  point_counter running_total_votes{};

  placing current_round_placing{};
  placing last_round_placing{};

  placing current_top_placing{};
  placing last_top_placing{};

  comparison_result round_comparison{};
  comparison_result top_comparison{};

  while (getline(cin, line)) {
    line_number++;
    instruction_type lineType = instruction_type_of_line(line);

    switch (lineType) {
    case instruction_type::max: {
      auto const &[valid, new_max_key] = parse_max(max_key, line);

      if (!valid) {
        printLineError(line, line_number);
        continue;
      }

      /*
        # zamykanie rundy
        last_round_placing <- current_round_placing # move
        current_round_placing := placing_of_votes(current_round_point_votes);

        round_comparison := comparison_of_placings(last_round_placing, current_round_placing)

        # calculating top
        last_top_placing <- current_top_placing
        running_total_votes := sum_up_votes(running_total_votes, current_round_votes)
        current_top_placing := placing_of_votes(running_total_votes)

        top_comparison := comparison_of_placings(last_top_placing, current_top_placing)

        # szykowanie nowych głosów
        eliminated_songs := eliminated_of_placings(last_round_placing, current_round_placing)
        current_round_votes := extend_votes(current_round_votes, max, new_max)
        current_round_votes := filter_eliminated(current_round_votes, eliminated_songs)

        print_comparison(round_comparison)
      */

    } break;
    case instruction_type::top: {
      // nic do sprawdzania, bo regexp validuje.

    } break;
    case instruction_type::vote: {
      auto const &[valid, parsed_votes] =
          parseVote(current_round_votes, max_key, line);

      if (!valid) {
        printLineError(line, line_number);
        continue;
      }

      for (auto vote_song_id : parsed_votes) {
        current_round_votes[vote_song_id] += 1;
      }
    } break;
    case instruction_type::empty: {
    } break;
    case instruction_type::unknown: {
      printLineError(line, line_number);
      continue;
    } break;
    default: {
      cerr << "Unknown instruction: " << line << "(" << lineType << ")\n";
      return 1;
    } break;
    }

    printf("max_key: %lu, instruction: %d, line: %s\n", max_key, lineType,
           line.c_str());
    // printStore(voteStores.second);
  }

  return 0;
}
