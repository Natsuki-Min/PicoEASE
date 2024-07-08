def process_data(data):
    lines = data.strip().split('\n')
    
    for line in lines:
        line = line.strip()
        if line.upper() == 'Q':
            break
        
        try:
            bytes_data = bytes.fromhex(line.zfill(8))  # Convert hex string to bytes and pad to 16 hex digits
            if len(bytes_data) == 4:  # Ensure bytes length is 8 (32 bits)
                part1 = bytes_data[0]  # 8bit (first byte)
                part2 = int.from_bytes(bytes_data[1:2], byteorder='big')>>1  # 7bit address (next 2 bytes)
                part3 = bytes_data[1] & 0x1  # 1bit R/W (high bit of fourth byte)
                part4 = int.from_bytes(bytes_data[2:], byteorder='big')  # 16bit data (last 2 bytes)
                
                process(part1, part2, part3, part4,bytes_data)
            else:
                print(f"Ignoring invalid input: {line}")
        except ValueError:
            print(f"Ignoring invalid input: {line}")

def process(part1, part2, part3, part4 ,line):
    if not part1:
        #print(line)
        if line==b'\x00\x00\x00\x00' or line==b'\x00\xff\xff\xff':
            print('begin')
        else:
            if part3:
                print(f"R {part2:X} {part4:X}")
            else:
                print(f"W {part2:X} {part4:X}")
    else:
        print('error')

# Example usage:
if __name__ == "__main__":
    data = '''10480
FFFFFF
CF0000
FFFFFF
CE0001
10480
0
10480
'''
    # Example data
    process_data(data)
