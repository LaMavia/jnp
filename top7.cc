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

using point_counter = unordered_map<uint64_t, uint64_t>;  // (1)
using placing = vector<uint64_t>;                         // (2)
using comparison = vector<pair<uint64_t, string>>;        // (3)

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
    :: vector<pair<song_id, ~d_pos: string>>
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
      {instruction_type::max, regex(R"(^NEW\s+\d+)")},
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

// T = O(n log(7)) = O(n)
// M = O(7) = O(1)
auto placing_of_votes(point_counter &votes) -> placing {
  // a > b
  static auto placing_cmp = [](const pair<uint64_t, uint64_t> a,
                               const pair<uint64_t, uint64_t> b) -> bool {
    return a.second > b.second || (a.second == b.second && a.first < b.first);
  };

  placing output{};
  set<pair<uint64_t, uint64_t>, decltype(placing_cmp)> intermediate_placing{};

  for (auto const &entry : votes) {
    if (entry.second == 0) {
      continue;
    }

    if (intermediate_placing.size() < 7) {
      intermediate_placing.insert(entry);
    } else {
      auto min_entry = *intermediate_placing.rbegin();

      if (placing_cmp(entry, min_entry)) {  // entry > min_entry
        intermediate_placing.erase(min_entry);
        intermediate_placing.insert(entry);
      }
    }
  }

  // place the intermediate in the output
  for (auto const &[song_id, _] : intermediate_placing) {
    output.push_back(song_id);
  }

  return output;
}

auto comparison_of_placings(placing &previous_placing, placing &current_placing)
    -> comparison {
  comparison output{};

  for (uint64_t i = 0; i < current_placing.size(); i++) {
    auto song_id = current_placing[i];
    int64_t current_position = i + 1;

    auto previous_position_it =
        find(previous_placing.begin(), previous_placing.end(), song_id);

    if (previous_position_it == previous_placing.end()) {
      output.push_back({song_id, "-"});
    } else {
      int64_t previous_position =
          previous_position_it - previous_placing.begin() + 1;

      output.push_back(
          {song_id, to_string(previous_position - current_position)});
    }
  }

  return output;
}

auto add_top_placing_votes(point_counter &top_placing_votes,
                           placing &current_round_placing) -> point_counter & {
  for (uint64_t i = 0; i < current_round_placing.size(); i++) {
    auto points = 7 - i;
    auto song_id = current_round_placing[i];

    top_placing_votes[song_id] += points;
  }

  return top_placing_votes;
}

auto eliminated_of_placings(placing &previous_placing, placing &current_placing)
    -> unordered_set<uint64_t> {
  unordered_set<uint64_t> output{};

  // song_id in previous_placing ∧ song_id not in current_placing => eliminated
  for (auto const &song_id : previous_placing) {
    if (find(current_placing.begin(), current_placing.end(), song_id) ==
        current_placing.end()) {
      output.insert(song_id);
    }
  }

  return output;
}

auto extend_votes(point_counter &current_round_votes, uint64_t old_max,
                  uint64_t new_max) -> point_counter & {
  for (uint64_t song_id = old_max + 1; song_id <= new_max; song_id++) {
    current_round_votes[song_id] = 0;
  }

  return current_round_votes;
}

auto filter_eliminated_songs(point_counter &current_round_votes,
                             unordered_set<uint64_t> &eliminated_songs)
    -> point_counter & {
  for (auto const &song_id : eliminated_songs) {
    current_round_votes.erase(song_id);
  }

  return current_round_votes;
}

auto clear_votes(point_counter &current_round_votes) -> point_counter & {
  for (auto const &[song_id, _] : current_round_votes) {
    current_round_votes[song_id] = 0;
  }

  return current_round_votes;
}

auto print_comparison(comparison &comp) -> void {
  for (auto const &[song_id, d_pos] : comp) {
    cout << song_id << " " << d_pos << "\n";
  }
}

auto main() -> int {
  string line;
  size_t max_key = 0;
  size_t line_number = 0;

  point_counter current_round_votes{};
  point_counter top_placing_votes{};

  placing current_round_placing{};
  placing last_round_placing{};

  placing current_top_placing{};
  placing last_top_placing{};

  comparison round_comparison{};
  comparison top_comparison{};

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

        // zamykanie rundy
        last_round_placing = move(current_round_placing);
        current_round_placing = placing_of_votes(current_round_votes);

        round_comparison =
            comparison_of_placings(last_round_placing, current_round_placing);

        // calculating top
        top_placing_votes =
            add_top_placing_votes(top_placing_votes, current_round_placing);

        // szykowanie nowych głosów
        auto eliminated_songs =
            eliminated_of_placings(last_round_placing, current_round_placing);
        current_round_votes =
            extend_votes(current_round_votes, max_key, new_max_key);
        current_round_votes =
            filter_eliminated_songs(current_round_votes, eliminated_songs);
        current_round_votes = clear_votes(current_round_votes);

        max_key = new_max_key;

        print_comparison(round_comparison);
      } break;
      case instruction_type::top: {
        // nic do sprawdzania, bo regexp validuje.
        last_top_placing = move(current_top_placing);
        current_top_placing = placing_of_votes(top_placing_votes);

        top_comparison =
            comparison_of_placings(last_top_placing, current_top_placing);

        print_comparison(top_comparison);

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
  }

  return 0;
}