// Header and license info goes here
// Copyright 2016 University of Bristol, all rights reserved
#ifndef MINI_PARSER_H
#define MINI_PARSER_H

#include <string>
#include <vector>

class MiniParser {
public:
	enum InputFormat {FORMAT_JSON, FORMAT_PYTHON};
	MiniParser(const char* in = "");
	~MiniParser();
	inline void set_input_format(InputFormat format) {input_format = format;}
	const char* find_tag(const char* tag);
	int get(const char* tag, uint32_t& out);
	int get(const char* tag, double& out);
	int get(const char* tag, float& out);
	int get(const char* tag, std::string& out);
	int get(const char* tag, std::vector<std::string>& out);
	int get(const char* tag, std::vector<float>& out);
	int get(const char* tag, std::vector<double>& out);
	int get(const char* tag, std::vector<int>& out);
	int get(const char* tag, std::vector<uint8_t>& out);
	int get(const char* tag, std::vector<std::vector<uint8_t> >& out);

	// Output to match the input
	void begin();
	void terminate();
	const size_t length() const { return out_str.size(); }
	const char* c_str() const { return out_str.c_str(); }
	const std::string& std_str() const { return out_str; }
	void append(const char* tag, double in_val);
	void append(const char* tag, float in_val);
	void append(const char* tag, uint64_t in_val);
	void append(const char* tag, uint32_t in_val);
	void append(const char* tag, int in_val);
	void append(const char* tag, const char* in_val);
	void append(const char* tag, const std::string& in_val) { append(tag, in_val.c_str()); }
	void append(const char* tag, std::vector<int>& in_val);
	void append(const char* tag, std::vector<uint32_t>& in_val);
	void append(const char* tag, std::vector<uint64_t>& in_val);
	void append(const char* tag, std::vector<double>& in_val);
	void append(const char* tag, std::vector<float>& in_val);
	void append(const char* tag, std::vector<const char*>& in_val);
	void append(const char* tag, std::vector<std::vector<int> >& in_val);
	void append(const char* tag, const std::vector<std::string>& in_val);

private:
	void scan_all_tags();
	template<typename T, typename scanT>
	int get_scan_array(const char* start, std::vector<T>& out, const char* scan_type, char const** out_end_ptr);
	template<typename T, typename scanT>
	int get_scan_array_of_arrays(const char* start, std::vector<std::vector<T> >& out, const char* scan_type, char const** out_end_ptr);
	void append_tag(const char* tag);

	const char* in_str;
	std::string out_str;
	size_t in_length;
	InputFormat input_format;
	std::vector<size_t> tag_indices;
};

#endif // MINI_PARSER_H

