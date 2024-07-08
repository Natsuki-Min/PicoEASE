def bin_to_cpp_arrays(input_file, output_file_prefix, array_name_prefix, chunk_size=0x10000):
    try:
        with open(input_file, 'rb') as f_in:
            data = f_in.read()
            
            num_chunks = (len(data) + chunk_size - 1) // chunk_size  # Calculate total number of arrays needed
            
            for i in range(num_chunks):
                chunk_start = i * chunk_size
                chunk_end = min((i + 1) * chunk_size, len(data))
                chunk_data = data[chunk_start:chunk_end]
                
                output_file = f"{output_file_prefix}_{i}.cpp"
                array_name = f"{array_name_prefix}_{i}"
                
                with open(output_file, 'w') as f_out:
                    f_out.write(f"// {output_file}\n\n")  # Write a comment with the output file name
                    f_out.write(f"const uint8_t {array_name}[] = {{\n")  # Start of C++ array declaration
                    
                    # Write data bytes in hexadecimal format, 16 bytes per line
                    for j in range(0, len(chunk_data), 16):
                        line = chunk_data[j:j+16]
                        hex_values = ', '.join([f"0x{byte:02X}" for byte in line])
                        f_out.write(f"    {hex_values},\n")
                        
                    f_out.write("};\n")  # End of C++ array declaration
                    
                    print(f"Successfully wrote C++ array to {output_file}")
    
    except IOError:
        print(f"Error: Could not open or write to file {input_file}")

# Example usage
if __name__ == "__main__":
    input_file = 'ROM.bin'   # Input binary file name
    output_file_prefix = 'output'  # Prefix for output C++ file names
    array_name_prefix = 'binary_data'  # Prefix for array names
    chunk_size = 0x10000  # Size of each array
