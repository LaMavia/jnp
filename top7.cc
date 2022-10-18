#include <iostream>
#include <regex>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace std;

using point_counter = unordered_map<uint64_t, uint64_t>;
using placing = vector<uint64_t>;
using comparison = vector<pair<uint64_t, string>>;

enum instruction_type { top = 1, max = 2, vote = 3, empty = 4, unknown = 0 };

auto instruction_type_of_line(string &line) -> instruction_type {
  const static map<instruction_type, regex> cases{
      {instruction_type::max, regex(R"(^\s*NEW\s+\d+\s*$)")},
      {instruction_type::top, regex(R"(^\s*TOP\s*$)")},
      {instruction_type::vote, regex(R"(^\s*(0*\d{1,9}\s+)*(0*\d{1,9})\s*$)")},
      {instruction_type::empty, regex(R"(^\s*$)")}};

  for (auto const &[instruction, re] : cases) {
    if (regex_match(line, re)) {
      return instruction;
    }
  }

  return instruction_type::unknown;
}

// Parsers

auto parse_vote(point_counter &current_votes, size_t max_key, string &line)
    -> pair<bool, unordered_set<uint64_t>> {
  unordered_set<uint64_t> votes{};
  stringstream line_stream(line);
  bool valid = true;

  uint64_t vote;

  while (line_stream >> vote) {
    if (!current_votes.contains(vote) || vote == 0 || vote > max_key ||
        votes.contains(vote)) {
      valid = false;
      break;
    }

    votes.insert(vote);
  }

  return {valid, votes};
}

auto parse_max(uint64_t max_key, string &line) -> pair<bool, uint64_t> {
  stringstream line_stream(line);
  bool valid = true;

  int64_t new_max_key;

  // ignore leading whitespaces
  while (isspace(line_stream.peek())) {
    line_stream.ignore(1);
  }

  // ignore "NEW "
  line_stream.ignore(((sizeof(char)) * 4), ' ');

  valid = !!(line_stream >> new_max_key) && (new_max_key >= (int64_t)max_key) &&
          (new_max_key <= 99999999) && (new_max_key >= 1);

  return {valid, new_max_key};
}

// end: parsers

auto print_line_error(string &line, size_t line_number) -> void {
  cerr << "Error in line " << line_number << ": " << line << "\n";
}

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

      if (placing_cmp(entry, min_entry)) { // entry > min_entry
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
  uint64_t max_key = 0;
  uint64_t line_number = 0;

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
        print_line_error(line, line_number);
        continue;
      }

      // close the current round
      last_round_placing = move(current_round_placing);
      current_round_placing = placing_of_votes(current_round_votes);

      round_comparison =
          comparison_of_placings(last_round_placing, current_round_placing);

      // add placement votes
      top_placing_votes =
          add_top_placing_votes(top_placing_votes, current_round_placing);

      // prepare a new voting
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
      // nothing to check, regex validates the whole line
      last_top_placing = move(current_top_placing);
      current_top_placing = placing_of_votes(top_placing_votes);

      top_comparison =
          comparison_of_placings(last_top_placing, current_top_placing);

      print_comparison(top_comparison);

    } break;
    case instruction_type::vote: {
      auto const &[valid, parsed_votes] =
          parse_vote(current_round_votes, max_key, line);

      if (!valid) {
        print_line_error(line, line_number);
        continue;
      }

      for (auto vote_song_id : parsed_votes) {
        current_round_votes[vote_song_id] += 1;
      }
    } break;
    case instruction_type::empty: {
    } break;
    case instruction_type::unknown: {
      print_line_error(line, line_number);
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