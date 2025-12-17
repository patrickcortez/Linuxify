import struct
import os

SECTOR_SIZE = 512
CLUSTER_SIZE = 4096
MAGIC = 0x4C564C46

# Struct Formats
# SuperBlock: magic(I), totalSectors(Q), clusterSize(I), rootDirCluster(Q), freeMapCluster(Q), freeMapSectors(Q), padding
SB_FMT = "<I Q I Q Q Q 472x"
SB_SIZE = 512

# DirEntry: name(32s), type(B), startCluster(Q), size(Q), attributes(I), padding(11x)
DIR_ENTRY_FMT = "<32s B Q Q I 11x"
DIR_ENTRY_SIZE = 64

# VersionEntry: versionName(32s), contentTableCluster(Q), isActive(B), padding(23x)
VER_ENTRY_FMT = "<32s Q B 23x"
VER_ENTRY_SIZE = 64

TYPE_FREE = 0
TYPE_FILE = 1
TYPE_LEVELED_DIR = 2
TYPE_SYMLINK = 3
TYPE_HARDLINK = 4

class SuperBlock:
    def __init__(self, data):
        self.magic, self.total_sectors, self.cluster_size, \
        self.root_dir_cluster, self.lat_start_cluster, \
        self.lat_sectors = struct.unpack(SB_FMT, data)

class DirEntry:
    def __init__(self, data):
        name_bytes, self.type, self.start_cluster, \
        self.size, self.attributes, self.ctime, self.mtime = struct.unpack(DIR_ENTRY_FMT, data)
        self.name = name_bytes.decode('utf-8').strip('\x00')
    
    def get_type_str(self):
        """Return human-readable type string"""
        if self.type == TYPE_FREE: return "<FREE>"
        elif self.type == TYPE_FILE: return "<FILE>"
        elif self.type == TYPE_LEVELED_DIR: return "<L-DIR>"
        elif self.type == TYPE_SYMLINK: return "<SYMLNK>"
        elif self.type == TYPE_HARDLINK: return "<HDLINK>"
        else: return "<UNKNOWN>"

class VersionEntry:
    def __init__(self, data):
        name_bytes, self.content_cluster, self.is_active = struct.unpack(VER_ENTRY_FMT, data)
        self.name = name_bytes.decode('utf-8').strip('\x00')

class FileSystem:
    def __init__(self, image_path):
        self.image_path = image_path
        self.f = None
        self.sb = None
        self.base_offset = 0


    def mount(self):
        path = self.image_path
        # Handle simple drive letters on Windows (e.g. "D" -> "\\.\D:")
        if os.name == 'nt':
            if len(path) == 1 and path.isalpha():
                path = f"\\\\.\\{path}:"
            elif len(path) == 2 and path[1] == ':' and path[0].isalpha():
                path = f"\\\\.\\{path}"

        # STRICT: Enforce physical disk (must start with \\.\)
        if not path.startswith("\\\\.\\"):
             print(f"Error: Invalid disk '{self.image_path}'. This tool only supports physical disks (e.g. 'D' or 'D:'). Image files are not supported.")
             return False

        try:
            # buffering=0 is crucial for physical drive access on some systems
            self.f = open(path, "rb", buffering=0)
            self.f.seek(0)
            data = self.f.read(SB_SIZE)
            if len(data) < SB_SIZE:
                print("Failed to read superblock.")
                return False
            self.sb = SuperBlock(data)
            if self.sb.magic != MAGIC:
                print(f"Invalid Magic: {hex(self.sb.magic)}")
                return False
            return True
        except PermissionError:
             print("Permission denied. Try running as Administrator.")
             return False
        except Exception as e:
            print(f"Mount error: {e}")
            return False

    def close(self):
        if self.f:
            self.f.close()

    def read_sector(self, sector_idx, count=1):
        self.f.seek(self.base_offset + (sector_idx * SECTOR_SIZE))
        return self.f.read(count * SECTOR_SIZE)
    
    def get_lat_entry(self, cluster):
        """Read LAT entry for given cluster"""
        lat_offset = cluster * 8
        sector_offset = lat_offset // SECTOR_SIZE
        entry_offset = lat_offset % SECTOR_SIZE
        
        sector_idx = (self.sb.lat_start_cluster * 8) + sector_offset
        data = self.read_sector(sector_idx)
        entry, = struct.unpack("<Q", data[entry_offset:entry_offset+8])
        return entry

    def get_chain(self, start_cluster):
        """Follow LAT chain from start cluster"""
        chain = []
        curr = start_cluster
        while curr != 0 and curr != 0xFFFFFFFFFFFFFFFF and curr != 0xFFFFFFFFFFFFFFFE:
            chain.append(curr)
            curr = self.get_lat_entry(curr)
            # Safety break
            if len(chain) > 100000: break 
        return chain

    def get_usage(self):
        # Count bits in free map (if using bitmap approach)
        # For LAT-based system, scan LAT for free entries
        total_clusters = self.sb.total_sectors // (CLUSTER_SIZE // SECTOR_SIZE)
        return 0, total_clusters * CLUSTER_SIZE

    def list_dir(self, content_cluster):
        """List directory entries following LAT chain"""
        entries = []
        chain = self.get_chain(content_cluster)
        
        for c in chain:
            data = self.read_sector(c * 8, 8)  # Read 8 sectors (1 cluster)
            for j in range(0, len(data), DIR_ENTRY_SIZE):
                chunk = data[j:j+DIR_ENTRY_SIZE]
                if len(chunk) < DIR_ENTRY_SIZE: break
                entry = DirEntry(chunk)
                if entry.type != TYPE_FREE:
                    entries.append(entry)
        return entries
    
    def list_levels(self, dir_cluster):
        """List levels (versions) following LAT chain"""
        levels = []
        chain = self.get_chain(dir_cluster)
        for c in chain:
             data = self.read_sector(c * 8, 8)
             for j in range(0, len(data), VER_ENTRY_SIZE):
                chunk = data[j:j+VER_ENTRY_SIZE]
                if len(chunk) < VER_ENTRY_SIZE: break
                ver = VersionEntry(chunk)
                if ver.is_active:
                    levels.append(ver)
        return levels

    def find_level(self, dir_cluster, level_name):
        """Find specific level by name following LAT chain"""
        chain = self.get_chain(dir_cluster)
        for c in chain:
             data = self.read_sector(c * 8, 8)
             for j in range(0, len(data), VER_ENTRY_SIZE):
                chunk = data[j:j+VER_ENTRY_SIZE]
                if len(chunk) < VER_ENTRY_SIZE: break
                ver = VersionEntry(chunk)
                if ver.is_active and ver.name == level_name:
                    return ver.content_cluster
        return 0

    def read_file(self, start_cluster, size):
        """Read file data following LAT chain"""
        chain = self.get_chain(start_cluster)
        res = bytearray()
        remaining = size
        
        for c in chain:
            if remaining <= 0: break
            to_read = min(remaining, CLUSTER_SIZE)
            data = self.read_sector(c * 8, 8)
            res.extend(data[:to_read])
            remaining -= to_read
            
        return bytes(res)
    
    def read_symlink_target(self, symlink_cluster):
        """Read target path from symlink cluster"""
        if symlink_cluster == 0:
            return None
        data = self.read_sector(symlink_cluster * 8, 8)
        # Target path stored as null-terminated string
        target = data.split(b'\x00', 1)[0].decode('utf-8', errors='ignore')
        return target
