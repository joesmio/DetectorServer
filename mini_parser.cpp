// Header and license info goes here
// Copyright 2016 University of Bristol, all rights reserved

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <float.h>
#include <cfloat>
#include <math.h>
#include <string.h>
#include <map>
#include <vector>
#include "mini_parser.h"

#pragma warning( disable : 4996 )

MiniParser::MiniParser(const char* in) : in_str(in)
{
	in_length = strlen(in_str);
	scan_all_tags();
}

MiniParser::~MiniParser()
{
}

void MiniParser::scan_all_tags()
{
	if (!in_str || in_length < 2)
		return;
	for (size_t i = 0; i < in_length - 1; ++i)
	{
		if (in_str[i] == '\"' && in_str[i + 1] != ':')
		{
			tag_indices.push_back(i);
		}
	}
}

const char* MiniParser::find_tag(const char* tag)
{
	size_t tag_len = strlen(tag);
	for (size_t index = 0; index < tag_indices.size(); ++index)
	{
		const char* test = in_str + tag_indices[index] + 1;
		if (!strncmp(tag, test, tag_len) && test[tag_len] == '\"')
			return test + tag_len + 2;
	}
/*
	const char* start = in_str;
	const char* end = start + in_length;
	const char* next = start + 1;
	bool done = false;
	while (next < end)
	{
		next = strstr(next, tag);
		if (!next)
			return NULL;
		if (next + tag_len + 2 >= end)
			return NULL;
		if (next[-1] != '\"' || next[tag_len] != '\"' || next[tag_len + 1] != ':')
		{
			next += tag_len + 1;
			continue;
		}
//		printf("Found tag '%s'\n", tag);
		return next + tag_len + 2;
	}
*/
	return NULL;
}

int MiniParser::get(const char* tag, uint32_t& out)
{
	int items_found = 0;
	const char* tag_pos = find_tag(tag);
	if (tag_pos)
		items_found = sscanf(tag_pos, " %d ", &out);
//	printf("...%d items\n", items_found);
	return items_found;
}

int MiniParser::get(const char* tag, double& out)
{
	int items_found = 0;
	const char* tag_pos = find_tag(tag);
	if (tag_pos)
		items_found = sscanf(tag_pos, " %lf ", &out);
	//	printf("...%d items\n", items_found);
	return items_found;
}

int MiniParser::get(const char* tag, float& out)
{
	int items_found = 0;
	const char* tag_pos = find_tag(tag);
	if (tag_pos)
		items_found = sscanf(tag_pos, " %f ", &out);
	//	printf("...%d items\n", items_found);
	return items_found;
}

int MiniParser::get(const char* tag, std::string& out)
{
	int items_found = 0;
	const char* tag_pos = find_tag(tag);
	if (!tag_pos)
		return 0;

	const char* start = strchr(tag_pos, '\"');
	if (!start)
		return 0;

	const char* end = strchr(start + 1, '\"');
	if (!end)
		return 0;

	out = "";
	start++;
	while (start < end)
	{
		//            printf("char '%c' \n", start[0]);
		out += *start++;   // TODO: a better strncpy
	}
	items_found++;
//	printf("...%d items\n", items_found);
	return items_found;
}

int MiniParser::get(const char* tag, std::vector<float>& out)
{
	int items_found = 0;
	const char* tag_pos = find_tag(tag);
	if (!tag_pos)
		return 0;

	const char* start = strchr(tag_pos, '[');
	if (!start)
		return 0;

	const char* end = strchr(start + 1, ']');
	if (!end)
		return 0;

	out.clear();
	start++;
	while (start < end)
	{
		float fval;
		int found = sscanf(start, " %f ", &fval);
		if (!found)
			break;
		out.push_back(fval);
		start = strchr(start + 1, ',');
		if (!start)
			break;
		start++;
		items_found++;
	}
//	printf("...%d items\n", items_found);
	return items_found;
}

int MiniParser::get(const char* tag, std::vector<double>& out)
{
	int items_found = 0;
	const char* tag_pos = find_tag(tag);
	if (!tag_pos)
		return 0;

	const char* start = strchr(tag_pos, '[');
	if (!start)
		return 0;

	const char* end = strchr(start + 1, ']');
	if (!end)
		return 0;

	out.clear();
	start++;
	while (start < end)
	{
		double fval;
		int found = sscanf(start, " %lf ", &fval);
		if (!found)
			break;
		out.push_back(fval);
		start = strchr(start + 1, ',');
		if (!start)
			break;
		start++;
		items_found++;
	}
	//	printf("...%d items\n", items_found);
	return items_found;
}

template<typename T, typename scanT>
int MiniParser::get_scan_array(const char* start, std::vector<T>& out, const char* scan_type, char const** out_end_ptr)
{
	int items_found = 0;
	if (!start)
		return 0;
	start = strchr(start, '[');
	if (!start)
		return 0;

	const char* end = strchr(start + 1, ']');
	if (!end)
		return 0;

	out.clear();
	start++;
	while (start < end)
	{
		scanT val;
		int found = sscanf(start, scan_type, &val);
		if (!found)
			break;
		out.push_back(val);
		start = strchr(start + 1, ',');
		if (!start)
			break;
		start++;
		items_found++;
	}
	if (out_end_ptr)
		*out_end_ptr = end + 1;
//	printf("...%d items\n", items_found);
	return items_found;
}

template<typename T, typename scanT>
int MiniParser::get_scan_array_of_arrays(const char* start, std::vector<std::vector<T> >& out, const char* scan_type, char const** out_end_ptr)
{
	int items_found = 0;
	if (!start)
		return 0;
	bool done = false;
	while (start && *start && !done)
	{
		char ch = *start;
		switch (ch) {
		case '[':
		case ',':
			out.resize(out.size() + 1);
			start++;
			get_scan_array<T, scanT>(start, out[out.size() - 1], " %u ", &start);
			items_found++;
			break;
		case ' ':
			start++;
			break;
		case ']':
			start++;
			done = true;
			break;
		case '\0':
		default:
			done = true;
			break;
		}
	}
	if (out_end_ptr)
		*out_end_ptr = start;
	return items_found;
}

int MiniParser::get(const char* tag, std::vector<int>& out)
{
	return get_scan_array<int, int>(find_tag(tag), out, " %d ", NULL);
}

int MiniParser::get(const char* tag, std::vector<uint8_t>& out)
{
	return get_scan_array<uint8_t, unsigned int>(find_tag(tag), out, " %u ", NULL);
}

int MiniParser::get(const char* tag, std::vector<std::vector<uint8_t> >& out)
{
	return get_scan_array_of_arrays<uint8_t, unsigned int>(find_tag(tag), out, " %u ", NULL);
}

int MiniParser::get(const char* tag, std::vector<std::string>& out)
{
	int items_found = 0;
	const char* tag_pos = find_tag(tag);
	if (!tag_pos)
		return 0;

	const char* start = strchr(tag_pos, '[');
	if (!start)
		return 0;

	const char* end = strchr(start + 1, ']');
	if (!end)
		return 0;

	out.clear();
	start++;

	std::string this_str;
	const char* scanner = start;
	bool in_quotes = false;
	while (scanner < end)
	{
		char ch = *scanner;
		switch (ch) {
		case '\0':
		case ']':
			start = end;
			scanner = end;
			break;
		case ',':
		case ' ':
			break;
		case '\"':
			if (!in_quotes)
			{
				start = scanner + 1;
				in_quotes = true;
			}
			else
			{
				while (start < scanner)
					this_str += *start++;
				out.push_back(this_str);
				this_str.clear();
				items_found++;
				in_quotes = false;
			}
		}
		scanner++;
	}
//	printf("...%d items\n", items_found);
	return items_found;
}

void MiniParser::begin()
{
	out_str = "{";
}

void MiniParser::terminate()
{
	// Remove any trailing comma
	if (out_str[out_str.size() - 1] == ',')
		out_str.resize(out_str.size() - 1);
	out_str += "}";
}

void MiniParser::append_tag(const char* tag)
{
	if (!tag)
		return;
	out_str += "\"";
	out_str += tag;
	out_str += "\":";
}

void MiniParser::append(const char* tag, double in_val)
{
	char str[256];
	append_tag(tag);
//	sprintf(str, "%.*e,", __DECIMAL_DIG__, in_val);
	sprintf(str, "%g,", in_val);
	out_str += str;
}

void MiniParser::append(const char* tag, float in_val)
{
	char str[256];
	append_tag(tag);
	sprintf(str, "%f,", in_val);
	out_str += str;
}

void MiniParser::append(const char* tag, uint32_t in_val)
{
	char str[256];
	append_tag(tag);
	sprintf(str, "%u,", (unsigned int)in_val);
	out_str += str;
}

void MiniParser::append(const char* tag, int in_val)
{
	char str[256];
	append_tag(tag);
	sprintf(str, "%d,", in_val);
	out_str += str;
}

void MiniParser::append(const char* tag, const char* in_val)
{
	append_tag(tag);
	out_str += "\"";
	out_str += in_val;
	out_str += "\",";
}

void MiniParser::append(const char* tag, std::vector<int>& in_val)
{
	char str[256];
	append_tag(tag);
	out_str += "[";

	if (in_val.size())
	{
		for (size_t i = 0; i < in_val.size(); ++i)
		{
			sprintf(str, "%d,", in_val[i]);
			out_str += str;
		}
		out_str.resize(out_str.size() - 1);
	}
	out_str += "],";
}

void MiniParser::append(const char* tag, std::vector<uint32_t>& in_val)
{
	char str[256];
	append_tag(tag);
	out_str += "[";

	if (in_val.size())
	{
		for (size_t i = 0; i < in_val.size(); ++i)
		{
			sprintf(str, "%u,", (unsigned int)in_val[i]);
			out_str += str;
		}
		out_str.resize(out_str.size() - 1);
	}
	out_str += "],";
}

void MiniParser::append(const char* tag, std::vector<uint64_t>& in_val)
{
	char str[256];
	append_tag(tag);
	out_str += "[";

	if (in_val.size())
	{
		for (size_t i = 0; i < in_val.size(); ++i)
		{
			sprintf(str, "%lu,", (long unsigned int)in_val[i]);
			out_str += str;
		}
		out_str.resize(out_str.size() - 1);
	}
	out_str += "],";
}

void MiniParser::append(const char* tag, std::vector<double>& in_val)
{
	char str[256];
	append_tag(tag);
	out_str += "[";

	if (in_val.size())
	{
		for (size_t i = 0; i < in_val.size(); ++i)
		{
//			sprintf(str, "%.*e,", __DECIMAL_DIG__, in_val[i]);
			sprintf(str, "%g,", in_val[i]);
			out_str += str;
		}
		out_str.resize(out_str.size() - 1);
	}
	out_str += "],";
}

void MiniParser::append(const char* tag, std::vector<float>& in_val)
{
	char str[256];
	append_tag(tag);
	out_str += "[";

	if (in_val.size())
	{
		for (size_t i = 0; i < in_val.size(); ++i)
		{
			sprintf(str, "%f,", in_val[i]);
			out_str += str;
		}
		out_str.resize(out_str.size() - 1);
	}
	out_str += "],";
}

void MiniParser::append(const char* tag, std::vector<const char*>& in_val)
{
	append_tag(tag);
	out_str += "[";

	if (in_val.size())
	{
		for (size_t i = 0; i < in_val.size(); ++i)
		{
			out_str += "\"";
			out_str += in_val[i];
			out_str += "\",";
		}
		out_str.resize(out_str.size() - 1);
	}
	out_str += "],";
}

void MiniParser::append(const char* tag, std::vector<std::vector<int> >& in_val)
{
	append_tag(tag);
	out_str += "[";

	if (in_val.size())
	{
		for (size_t i = 0; i < in_val.size(); ++i)
		{
			append(NULL, in_val[i]);
		}
		out_str.resize(out_str.size() - 1);
	}
	out_str += "],";
}

void MiniParser::append(const char* tag, const std::vector<std::string>& in_val)
{
	char str[4096];
	append_tag(tag);
	out_str += "[";

	if (in_val.size())
	{
		for (size_t i = 0; i < in_val.size(); ++i)
		{
			sprintf(str, "\"%s\",", in_val[i].c_str());
			out_str += str;
		}
		out_str.resize(out_str.size() - 1);
	}
	out_str += "],";
}


