#include <cassert>
#include <iostream>
#include <regex>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using std::cerr, std::cin, std::cout;
using std::uint64_t;
using std::vector, std::unordered_map, std::pair, std::string, std::to_string,
    std::regex, std::unordered_set, std::set, std::stringstream;

using song_id_t = uint64_t;
using point_counter = unordered_map<song_id_t, uint64_t>;
using placing = vector<song_id_t>;
using comparison = vector<pair<song_id_t, string>>;

enum instruction_type { top = 1, max = 2, vote = 3, empty = 4, unknown = 0 };

auto instruction_type_of_line(string &line) -> instruction_type {
  const static unordered_map<instruction_type, regex> cases{
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

auto parse_vote(point_counter &current_votes, uint64_t max_key, string &line)
    -> pair<bool, unordered_set<song_id_t>> {
  unordered_set<song_id_t> votes{};
  stringstream line_stream(line);
  bool valid = true;

  song_id_t vote;

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

  uint64_t new_max_key;

  // ignore leading whitespaces
  while (isspace(line_stream.peek())) {
    line_stream.ignore(1);
  }

  // ignore "NEW "
  line_stream.ignore(((sizeof(char)) * 4), ' ');

  valid = !!(line_stream >> new_max_key) && (new_max_key >= max_key) &&
          (new_max_key <= 99999999) && (new_max_key >= 1);

  return {valid, new_max_key};
}

// end: parsers

auto print_line_error(string &line, uint64_t line_number) -> void {
  cerr << "Error in line " << line_number << ": " << line << "\n";
}

auto placing_of_votes(point_counter &votes) -> placing {
  // a > b
  static auto placing_cmp = [](const pair<song_id_t, uint64_t> &a,
                               const pair<song_id_t, uint64_t> &b) -> bool {
    return a.second > b.second || (a.second == b.second && a.first < b.first);
  };

  placing output{};
  // set instead of an unordered_set for quickly obtaining the min element
  set<pair<song_id_t, uint64_t>, decltype(placing_cmp)> intermediate_placing{};

  for (auto const &entry : votes) {
    // ignore songs with 0 votes
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
    // 0 ≤ i < 7 << MAX_UINT64, so the cast is safe
    int64_t current_position = i + 1;

    auto previous_position_it =
        find(previous_placing.begin(), previous_placing.end(), song_id);

    // the song wasn't in the previous placing
    if (previous_position_it == previous_placing.end()) {
      output.push_back({song_id, "-"});
    }
    // calculate the placing difference
    else {
      int64_t previous_position =
          previous_position_it - previous_placing.begin() + 1;

      output.push_back(
          {song_id, to_string(previous_position - current_position)});
    }
  }

  return output;
}

// Add votes for placing in the placings to the "top" counter
auto add_top_placing_votes(point_counter &top_votes,
                           placing &current_round_placing) -> point_counter & {
  for (uint64_t i = 0; i < current_round_placing.size(); i++) {
    auto points = 7 - i;
    auto song_id = current_round_placing[i];

    top_votes[song_id] += points;
  }

  return top_votes;
}

// determine which songs have been eliminated in the current round
auto eliminated_of_placings(placing &previous_placing, placing &current_placing)
    -> unordered_set<song_id_t> {
  unordered_set<song_id_t> output{};

  // song_id in previous_placing ∧ song_id not in current_placing => eliminated
  for (auto const &song_id : previous_placing) {
    if (find(current_placing.begin(), current_placing.end(), song_id) ==
        current_placing.end()) {
      output.insert(song_id);
    }
  }

  return output;
}

//
auto eliminated_from_top(placing &previous_top_placing,
                         placing &current_top_placing,
                         point_counter &current_round_votes)
    -> unordered_set<song_id_t> {

  unordered_set<song_id_t> output{};

  // song_id in previous_placing    ∧ (1)
  // song_id not in current_placing ∧ (2)
  // song_id not in current_voting    (3)
  // => eliminate, since it's placed below 7 (2), and can't go up (3)
  for (auto const &song_id : previous_top_placing) {
    if (find(current_top_placing.begin(), current_top_placing.end(), song_id) ==
            current_top_placing.end() &&
        !current_round_votes.contains(song_id)) {
      output.insert(song_id);
    }
  }

  return output;
}

// add new songs to the next round's vote counter
auto extend_votes(point_counter &current_round_votes, uint64_t old_max,
                  uint64_t new_max) -> point_counter & {
  for (song_id_t song_id = old_max + 1; song_id <= new_max; song_id++) {
    current_round_votes[song_id] = 0;
  }

  return current_round_votes;
}

// remove eliminated songs from the current round's vote counter
auto filter_eliminated_songs(point_counter &votes,
                             unordered_set<song_id_t> &eliminated_songs)
    -> point_counter & {
  for (auto const &song_id : eliminated_songs) {
    votes.erase(song_id);
  }

  return votes;
}

// reset vote counters of all the songs
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
  point_counter top_votes{};

  placing current_round_placing{};
  placing current_top_placing{};

  while (getline(cin, line)) {
    line_number++;
    instruction_type line_type = instruction_type_of_line(line);

    switch (line_type) {
    case instruction_type::max: {
      auto const &[valid, new_max_key] = parse_max(max_key, line);

      if (!valid) {
        print_line_error(line, line_number);
        continue;
      }

      // close the current round
      placing last_round_placing = move(current_round_placing);
      current_round_placing = placing_of_votes(current_round_votes);

      comparison round_comparison =
          comparison_of_placings(last_round_placing, current_round_placing);

      // add placement points
      top_votes = add_top_placing_votes(top_votes, current_round_placing);

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

      placing last_top_placing = move(current_top_placing);
      current_top_placing = placing_of_votes(top_votes);

      comparison top_comparison =
          comparison_of_placings(last_top_placing, current_top_placing);

      auto eliminated_top_songs = eliminated_from_top(
          last_top_placing, current_top_placing, current_round_votes);
      top_votes = filter_eliminated_songs(top_votes, eliminated_top_songs);

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
      stringstream error_message_stream{};

      error_message_stream
          << "Uncovered instruction_type case in main's switch. Type = "
          << line_type << ", line = \'" << line << "'\n";

      assert(((void)error_message_stream.str(), false));
      return 1;
    } break;
    }
  }

  return 0;
}