#include <algorithm>
#include <iostream>
#include <fstream>
#include <string>
#include <string_view>
#include <cctype>
#include <unordered_map>
#include <vector>
#include <cmath>
#include <execution>
#include <mutex>
#include <ranges>

bool is_string_alpha(std::string& str) {
	for (char& c : str) {
		if (!std::isalpha(c)) {
			return false;
		}

		c = std::tolower(c);
	}
	return true;
}

void update_word_maps(
	std::unordered_map<std::string, unsigned int>& word_to_int,
	std::unordered_map<unsigned int, std::string>& int_to_word,
	std::string& str) {
	if (word_to_int.find(str) == word_to_int.end()) {
		unsigned int idx = word_to_int.size();
		int_to_word[idx] = str;
		word_to_int[str] = idx;
	}
}

template<typename T>
std::vector<std::vector<size_t>> chunk_builder(const std::vector<T>& data, const size_t& chunk_size) {
	size_t data_size{ data.size() }, tot_chunks{ static_cast<size_t>(std::llroundl(static_cast<long double>(data_size) / static_cast<long double>(chunk_size))) };
	std::vector<std::vector<size_t>> chunks(tot_chunks);

	for (size_t chunk_idx{}; chunk_idx < tot_chunks; chunk_idx++) {
		for (size_t idx{}; idx < chunk_size && chunk_idx * chunk_size + idx < data_size; idx++) {
			chunks[chunk_idx].push_back(chunk_idx * chunk_size + idx);
		}
	}

	return chunks;
}

int main(int argc, char* argv[]) {
	//if (argc != 3) {
	//	std::cerr << "Usage: " << argv[0] << " <corpus.txt> <matrix.txt>\n";
	//	return 1;
	//}

	//std::string_view in_file_path = argv[1];
	//std::string_view out_file_path = argv[2];

	//std::cout << "Corpus file: " << in_file_path << '\n';
	//std::cout << "Matrix file: " << out_file_path << '\n';

	constexpr const char* in_file_path = "C:\\corpora\\brown\\words.txt";
	 constexpr const char* out_file_path = "C:\\corpora\\brown\\words-matrix-test.txt";

	// Open input and output files
	std::fstream ifile(in_file_path, std::ios_base::in);
	std::fstream ofile(out_file_path, std::ios_base::out);

	// Check if files are open
	if (!ifile.is_open() || !ofile.is_open()) {
		std::cout << "File could not be opened\n";
		return 1;
	}

	// Initialize data structures
	std::unordered_map<std::string, unsigned int> word_to_int{};
	std::unordered_map<unsigned int, std::string> int_to_word{};
	std::vector<unsigned int> words{};
	std::string line{};

	std::cout << "[INFO] Reading words from '" << in_file_path << "'...\n";

	// Read words from input file
	while (std::getline(ifile, line)) {
		if (!is_string_alpha(line)) {
			continue;
		}

		update_word_maps(word_to_int, int_to_word, line);

		// Store index of word
		words.push_back(word_to_int[line]);
	}

	// Close input file
	ifile.close();

	// Count total and unique words
	size_t wrd_cnt{ words.size() }, unq_cnt{ word_to_int.size() };

	std::cout << "[INFO] Finished reading words.\n";

	// Initialize co-occurrence matrix
	std::vector<std::unordered_map<unsigned int, std::atomic<unsigned int>>> occurrence_matrix(unq_cnt);
	std::vector<std::vector<size_t>> chunks = chunk_builder(words, 8192);

	std::cout << "[INFO] Building co-occurrence matrix..." << std::endl;

	std::for_each(std::execution::par, chunks.begin(), chunks.end(), [&](const std::vector<size_t> indexs) {
		for (size_t idx{}; idx < indexs.size(); idx++) {
			for (std::size_t offset{ idx - (idx > 1 ? 2 : (idx > 0 ? 1 : 0)) }; offset <= idx + (idx + 2 < wrd_cnt ? 2 : (idx + 1 < wrd_cnt ? 1 : 0)); offset++) {
				occurrence_matrix[words[idx]][words[offset]]++;
			}
		}
		});

	// Build co - occurrence matrix
	//for (size_t index{}; index <= wrd_cnt; index++) {
	//	for (size_t offset{ index - (index > 1 ? 2 : (index > 0 ? 1 : 0)) }; offset <= index + (index + 2 < wrd_cnt ? 2 : (index + 1 < wrd_cnt ? 1 : 0)); offset++) {
	//		occurrence_matrix[words[index]][words[offset]]++;
	//	}

	//	// Print progress
	//	std::cout << "\rProgress: " << static_cast<unsigned int>(std::roundf(static_cast<float>(index) / static_cast<float>(wrd_cnt) * 100u)) << "%";
	//}

	std::cout << "\rProgress: 100%" << std::flush << "\n[INFO] Finished building co-occurrence matrix.\n[INFO] Writing co-occurrence matrix to '" << out_file_path << "'...\n";

	for (size_t word{}; word < unq_cnt; word++) {
		ofile << int_to_word[word] << ":" << word << " ";
	}
	ofile << "\n";

	for (size_t word{}; word < unq_cnt; word++) {
		for (size_t occurrence{}; occurrence < unq_cnt; occurrence++) {
			if (occurrence_matrix[word].find(occurrence) != occurrence_matrix[word].end()) {
				ofile << occurrence << ":" << occurrence_matrix[word][occurrence] << " ";
			}
		}
		ofile << "\n";

		// Print progress
		std::cout << "\rProgress: " << static_cast<unsigned int>(std::roundf(static_cast<float>(word) / static_cast<float>(unq_cnt) * 100u)) << "%";
	}

	// Close outputfile
	ofile.close();

	std::cout << "\rProgress: 100%" << std::flush << "\n[INFO] Finished writing co-occurrence matrix.\n";

	return 0;
}