import numpy as np
import zstd
import json
import os

def _int_to_sleb128_reg(val):
    """Encode an integer to signed LEB128."""
    result = bytearray()
    more = True
    while more:
        byte = val & 0x7F
        val >>= 7
        
        # Check if we are done:
        # For positive: remaining bits are 0 and sign bit of current byte is 0
        # For negative: remaining bits are -1 and sign bit of current byte is 1
        sign_bit = byte & 0x40
        if (val == 0 and not sign_bit) or (val == -1 and sign_bit):
            more = False
        else:
            byte |= 0x80
        result.append(byte)
    return result


def save_compressed(file_path, normalized_y_values, total_investment, smooth_value):
    # Step 1: Scale to 8 decimal points and split into two parts
    scaled_8 = np.round(normalized_y_values * 1e8).astype(np.int64)
    first_part = (scaled_8 // 10000).astype(np.int64)
    second_part = (scaled_8 % 10000).astype(np.int64)
    
    # Step 2: Delta encoding
    deltas1 = np.empty_like(first_part)
    deltas1[0] = first_part[0]
    deltas1[1:] = first_part[1:] - first_part[:-1]
    
    deltas2 = np.empty_like(second_part)
    deltas2[0] = second_part[0]
    deltas2[1:] = second_part[1:] - second_part[:-1]
    
    # Step 3: Signed LEB128 encoding
    encoded_bytes1 = bytearray()
    for delta in deltas1:
        encoded_bytes1.extend(_int_to_sleb128_reg(delta))
    
    encoded_bytes2 = bytearray()
    for delta in deltas2:
        encoded_bytes2.extend(_int_to_sleb128_reg(delta))
    
    # Step 4: Zstandard compression
    cctx = zstd.ZstdCompressor(level=22)
    compressed1 = cctx.compress(encoded_bytes1)
    compressed2 = cctx.compress(encoded_bytes2)
    
    metadata = {
        "total_investment": total_investment,
        "smooth_value": smooth_value,
        "n": len(scaled_8),
        "format": "delta+leb128+zstd+split8"
    }
    
    print(f"Saving to {file_path}")
    with open(file_path, 'wb') as f:
        meta_json = json.dumps(metadata).encode('utf-8')
        f.write(len(meta_json).to_bytes(4, 'big'))
        f.write(meta_json)
        f.write(len(compressed1).to_bytes(4, 'big'))
        f.write(compressed1)
        f.write(len(compressed2).to_bytes(4, 'big'))
        f.write(compressed2)

if __name__ == "__main__":
    # Generate dummy datum
    # Simulate: val = T * (exp(y) - 1) => y = log((val + T)/T)
    T = 1000.0
    vals = np.linspace(0, 100, 1000) # Price goes from 0 to 100 profit?
    # Actually vals is the raw value?
    # The python code used: np.log((downsampled.values + total_investment) / (total_investment))
    # So let's assume 'vals' are the raw profit/loss values.
    
    log_vals = np.log((vals + T) / T)
    
    file_name = "test_signal.dsp"
    save_compressed(file_name, log_vals, T, 5)
    print("Done.")
