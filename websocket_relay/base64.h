

#ifndef BASE64_H
#define BASE64_H

#include <string>
#include <vector>

std::string base64_encode(const unsigned char *data, size_t input_length);
unsigned char *base64_decode(const char *data, size_t input_length, size_t *output_length);

size_t base64_encoded_length(size_t input_length);
size_t base64_encode_into_mem(const unsigned char *data, size_t input_length, char* out_encoded_data);

#endif // BASE64_H

