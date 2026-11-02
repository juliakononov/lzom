# LZOM - LZO Scatter-Gather Implementation

An extended implementation of the LZO compression algorithm for the Linux kernel with support for scatter-gather buffers (`struct bio_vec`). Wraps a block device and compresses its bios without linearizing data into a flat buffer first.

See `experiment/` for a benchmark of LZOM against flat-buffer LZO1X.

## Installation

1. Build the module:
```bash
   make
```

2. Load the module:
```bash
   sudo insmod lzom_module.ko
```

## Usage

```bash
   echo -n "<path_to_your_block_device>" > /sys/module/lzom_module/parameters/path
```

## Testing

Run the automated tests:
```bash
   sudo ./test/run_tests.sh
```