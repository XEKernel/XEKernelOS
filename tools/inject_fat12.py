"""Inject a file into an existing FAT12 disk image (root directory only)."""
import struct, sys, os

def inject_fat12(disk_path, file_path, fat12_name):
    with open(disk_path, 'r+b') as f:
        # Read BPB
        f.seek(0)
        bpb = f.read(512)
        bytes_per_sec = struct.unpack_from('<H', bpb, 11)[0]
        sec_per_cluster = bpb[13]
        reserved_sec = struct.unpack_from('<H', bpb, 14)[0]
        num_fats = bpb[16]
        root_entries = struct.unpack_from('<H', bpb, 17)[0]
        sec_per_fat = struct.unpack_from('<H', bpb, 22)[0]

        # Calculate offsets
        fat_start = reserved_sec * bytes_per_sec
        root_start = fat_start + num_fats * sec_per_fat * bytes_per_sec
        data_start = root_start + root_entries * 32

        # Read file to inject
        with open(file_path, 'rb') as fin:
            file_data = fin.read()
        file_size = len(file_data)
        clusters_needed = (file_size + sec_per_cluster * bytes_per_sec - 1) // (sec_per_cluster * bytes_per_sec)

        # Find free clusters in FAT
        f.seek(fat_start)
        fat_data = bytearray(f.read(sec_per_fat * bytes_per_sec))
        free_clusters = []
        for cluster in range(2, sec_per_fat * bytes_per_sec * 2 // 3):  # FAT12: 1.5 bytes per entry
            off = cluster * 3 // 2
            if cluster % 2 == 0:
                val = struct.unpack_from('<H', fat_data, off)[0] & 0xFFF
            else:
                val = struct.unpack_from('<H', fat_data, off)[0] >> 4
            if val == 0:  # Free cluster
                free_clusters.append(cluster)

        if len(free_clusters) < clusters_needed:
            print(f"ERROR: Need {clusters_needed} clusters, only {len(free_clusters)} free")
            return False

        # Allocate clusters and write data
        cluster_size = sec_per_cluster * bytes_per_sec
        for i in range(clusters_needed):
            cluster = free_clusters[i]
            # Write data
            sector = (cluster - 2) * sec_per_cluster + (data_start // bytes_per_sec)
            f.seek(sector * bytes_per_sec)
            chunk = file_data[i * cluster_size : (i + 1) * cluster_size]
            f.write(chunk)
            # Mark cluster in FAT
            off = cluster * 3 // 2
            if cluster % 2 == 0:
                old = struct.unpack_from('<H', fat_data, off)[0]
                next_val = free_clusters[i + 1] if i + 1 < clusters_needed else 0xFFF
                new = (old & 0xF000) | next_val
                struct.pack_into('<H', fat_data, off, new)
            else:
                old = struct.unpack_from('<H', fat_data, off)[0]
                next_val = free_clusters[i + 1] if i + 1 < clusters_needed else 0xFFF
                new = (old & 0x000F) | (next_val << 4)
                struct.pack_into('<H', fat_data, off, new)

        # Write FAT back
        f.seek(fat_start)
        f.write(fat_data)

        # Add root directory entry
        f.seek(root_start)
        for i in range(root_entries):
            entry = f.read(32)
            if entry[0] in (0x00, 0xE5):  # Free entry
                # Build 8.3 name
                name_bytes = bytearray(11)
                base, ext = os.path.splitext(fat12_name)
                # Pad name with spaces
                for j, c in enumerate(base[:8].upper()):
                    name_bytes[j] = ord(c)
                for j in range(len(base), 8):
                    name_bytes[j] = 0x20
                # Extension (without dot, pad to 3)
                ext = ext[1:] if ext.startswith('.') else ext
                for j, c in enumerate(ext[:3].upper()):
                    name_bytes[8 + j] = ord(c)
                for j in range(len(ext), 3):
                    name_bytes[8 + j] = 0x20

                f.seek(root_start + i * 32)
                f.write(name_bytes)                         # name (11)
                f.write(b'\x20')                            # attr (archive)
                f.write(b'\x00' * 10)                       # reserved
                f.write(struct.pack('<HH', 0, 0))           # time, date
                f.write(struct.pack('<H', free_clusters[0])) # first cluster
                f.write(struct.pack('<I', file_size))       # size
                print(f"Added {fat12_name} ({file_size} bytes, {clusters_needed} clusters)")
                return True

        print("ERROR: No free directory entry")
        return False

if __name__ == '__main__':
    disk = sys.argv[1] if len(sys.argv) > 1 else 'build/disk.img'
    src  = sys.argv[2] if len(sys.argv) > 2 else 'build/font_cn.bin'
    ok = inject_fat12(disk, src, 'FONT_CN.BIN')
    sys.exit(0 if ok else 1)
