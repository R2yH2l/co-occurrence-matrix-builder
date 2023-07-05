#include <iostream>
#include <fstream>
#include <string>
#include <cctype>
#include <unordered_map>
#include <vector>
#include <cmath>
#include <mutex>
#include <thread>
#include <queue>
#include <sstream>
#include <future>
#include <utility>
#include <any>

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
	std::unordered_map<std::string, unsigned long long>& word_to_int,
	std::unordered_map<unsigned long long, std::string>& int_to_word,
	std::string& str) {
	if (word_to_int.find(str) == word_to_int.end()) {
		unsigned long long idx = word_to_int.size();
		int_to_word[idx] = str;
		word_to_int[str] = idx;
	}
}

template<typename _t>
std::vector<std::vector<size_t>> chunkinator(const std::vector<_t>& data, const size_t& chunk_size) {
	size_t data_size{ data.size() }, tot_chunks{ static_cast<size_t>(std::llroundl(static_cast<long double>(data_size) / static_cast<long double>(chunk_size))) };
	std::vector<std::vector<size_t>> chunks(tot_chunks);

	for (size_t chunk_idx{}; chunk_idx < tot_chunks; chunk_idx++) {
		for (size_t idx{}; idx < chunk_size && chunk_idx * chunk_size + idx < data_size; idx++) {
			chunks[chunk_idx].emplace_back(chunk_idx * chunk_size + idx);
		}
	}

	return chunks;
}


/*
* This class provides a thread safe message queue.
* It's primary use is to handle out of order thread completions.
*/
template<typename _t>
class message_queue {
	std::queue<_t> m_queue{};
	mutable std::mutex m_mutex{};
	std::condition_variable m_condition{};

public:

	// Guard and push message to the end of queue
	void push(_t const& message) {
		std::lock_guard<std::mutex> lock(m_mutex);
		m_queue.push(message);
		m_condition.notify_one();
	}

	// Guard and check if queue is empty
	bool empty() const {
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_queue.size();
	}

	// Guard and retun first message if one exists
	bool try_pop(_t& popped_message) {
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_queue.empty()) {
			return false;
		}

		popped_message = m_queue.front();
		m_queue.pop();
		return true;
	}

	// Guard and wait for first available message
	void wait_and_pop(_t& popped_message) {
		std::unique_lock<std::mutex> lock(m_mutex);
		while (m_queue.empty()) {
			m_condition.wait(lock);
		}

		popped_message = m_queue.front();
		m_queue.pop();
	}
};

int main(int argc, char* argv[]) {
	if (argc != 3) {
		std::cerr << "Usage: " << argv[0] << " <corpus.txt> <matrix.txt>\n";
		return 1;
	}
	
	std::string_view in_file_path = argv[1];
	std::string_view out_file_path = argv[2];
	
	std::cout << "Corpus file: " << in_file_path << '\n';
	std::cout << "Matrix file: " << out_file_path << '\n';

	//constexpr const char* in_file_path = "D:\\corpora\\brown\\words.txt";
	//constexpr const char* out_file_path = "D:\\corpora\\brown\\words-matrix-test.txt";

	// Open input and output files
	std::fstream ifile(in_file_path.data(), std::ios_base::in);
	std::fstream ofile(out_file_path.data(), std::ios_base::out);

	// Check if files are open
	if (!ifile.is_open() || !ofile.is_open()) {
		std::cout << "File could not be opened\n";
		return 1;
	}

	// Initialize data structures
	std::unordered_map<std::string, unsigned long long> word_to_int{};
	std::unordered_map<unsigned long long, std::string> int_to_word{};
	std::vector<unsigned long long> words = {};
	std::string line{};

	std::cout << "[INFO] Reading words from '" << in_file_path << "'...\n";

	// Read words from input file
	while (std::getline(ifile, line)) {
		if (!is_string_alpha(line)) {
			continue;
		}

		update_word_maps(word_to_int, int_to_word, line);

		// Store index of word
		words.emplace_back(word_to_int[line]);
	}

	// Close input file
	ifile.close();

	// Count total and unique words
	size_t tot_words{ words.size() }, tot_unique{ word_to_int.size() };

	std::cout << "[INFO] Finished reading words.\n";

	// Initialize co-occurrence matrix
	std::vector<std::unordered_map<unsigned long long, unsigned long long>> occurrences(tot_unique);
	std::vector<std::mutex> occurrence_lock(tot_unique);

	{
		size_t chunk_size{ 8192 };
		std::vector<std::vector<size_t>> chunks = chunkinator(words, chunk_size);
		size_t tot_chunks{ chunks.size() };

		// Thread initialization
		std::vector<std::jthread> threads{};
		size_t tot_threads{ tot_chunks };

		message_queue<size_t> completed_threads{};

		// Lambda function for a thread
		auto thread_lambda = [&words, &tot_words, &occurrences, &occurrence_lock, &completed_threads]
		(const std::vector<size_t>& chunk, const size_t& chunk_idx) {
			for (std::vector<size_t>::const_iterator idx{ chunk.begin() }; idx != chunk.end(); idx++) {
				const unsigned long long word{ words[*idx] };

				// Lock current word
				std::lock_guard<std::mutex> grd{ occurrence_lock[word] };

				for (size_t offset{ *idx - (*idx > 1 ? 2 : *idx > 0 ? 1 : 0) }; offset < *idx - (*idx < tot_words - 1 ? 0 : *idx < tot_words ? 1 : 2); offset++) {
					occurrences[word][words[offset]]++;
				}
			}

			completed_threads.push(chunk_idx);
		};

		std::cout << "[INFO] Building co-occurrence matrix..." << std::endl;

		// Dispatch threads
		for (size_t idx{}; idx < tot_threads; idx++) {
			threads.emplace_back(thread_lambda, chunks[idx], idx);
		}

		size_t completed_cnt{}, message{};

		while (completed_cnt < tot_threads) {
			if (!completed_threads.try_pop(message)) {
				completed_threads.wait_and_pop(message);
			}

			completed_cnt++;

			std::cout << "\rProgress: " << std::llroundl(static_cast<long double>(completed_cnt) / static_cast<long double>(tot_threads) * 100ull) << "%";
		}
	}

	std::cout << "\n\n[INFO] Finished building co-occurrence matrix.\n[INFO] Assembling occurrences for output..." << std::endl;

	for (size_t idx{}; idx < tot_unique; idx++) {
		ofile << idx << ":" << int_to_word[idx] << " ";
	}
	ofile << "\n";

	{
		std::vector<std::vector<size_t>> chunks{ chunkinator(occurrences, 512) };
		size_t tot_chunks{ chunks.size() };

		// Thread initialization
		std::vector<std::jthread> threads{};
		size_t tot_threads{ tot_chunks };

		message_queue<std::pair<size_t, std::vector<std::string>>> completed_threads{};

		// Lambda function for a thread
		auto thread_lambda = [&words, &tot_words, &occurrences, &occurrence_lock, &completed_threads]
		(const std::vector<size_t>& chunk, const size_t& chunk_idx) {
			std::vector<std::string> word_occurrences{};

			for (std::vector<size_t>::const_iterator idx{ chunk.begin() }; idx != chunk.end(); idx++) {
				std::ostringstream ocurr{};

				for (auto& occurrence : occurrences[*idx]) {
					ocurr << occurrence.first << ":" << occurrence.second << " ";
				}

				word_occurrences.push_back(ocurr.str());
			}

			completed_threads.push(std::make_pair(chunk_idx, word_occurrences));
		};

		// Dispatch threads
		for (size_t idx{}; idx < tot_threads; idx++) {
			threads.emplace_back(thread_lambda, chunks[idx], idx);
		}

		size_t completed_cnt{};
		std::vector<std::vector<std::string>> word_order(tot_threads);
		std::pair<size_t, std::vector<std::string>> message{};

		while (completed_cnt < tot_threads) {
			if (!completed_threads.try_pop(message)) {
				completed_threads.wait_and_pop(message);
			}

			word_order[message.first] = message.second;

			completed_cnt++;

			std::cout << "\rProgress: " << std::llroundl(static_cast<long double>(completed_cnt) / static_cast<long double>(tot_threads) * 100ull) << "%";
		}

		std::cout << "\n\n[INFO] Occurrences ready for output.\n[INFO] Writing matrix to '" << out_file_path << "'." << std::endl;

		size_t written_cnt{};

		for (auto& chunk : word_order) {
			for (auto& occurrence : chunk) {
				ofile << occurrence << "\n";
			}

			written_cnt += chunk.size();

			std::cout << "\rProgress: " << std::llroundl(static_cast<long double>(written_cnt) / static_cast<long double>(tot_unique) * 100ull) << "%";
		}

		// Close outputfile
		ofile.close();

		std::cout << "\n\n[INFO] matrix written to file.\n";
	}

	return 0;
}