# fs_driver.py
# Linuxify LevelFS v2 Driver
# HLAT (Hierarchical Level Allocation Table) Architecture
#
# Run: python -c "import fs_driver; fs = fs_driver.FileSystem('D'); fs.mount()"

import struct
import os

SECTOR_SIZE = 512
CLUSTER_SIZE = 4096
SECTORS_PER_CLUSTER = 8
LFS_MAGIC = 0x4C465332
LFS_VERSION = 2

LAT_FREE = 0x0000000000000000
LAT_END = 0xFFFFFFFFFFFFFFFF
LAT_BAD = 0xFFFFFFFFFFFFFFFE
LIT_EMPTY = 0x0000000000000000

LAB_ENTRIES_PER_CLUSTER = 256
CLUSTERS_PER_LIT_ENTRY = 256

TYPE_FREE = 0
TYPE_FILE = 1
TYPE_LEVELED_DIR = 2
TYPE_SYMLINK = 3
TYPE_HARDLINK = 4
TYPE_LEVEL_MOUNT = 5

SB_FMT = "<II Q I Q  Q Q  Q Q Q  Q Q  Q Q Q  Q Q Q  Q  Q  Q Q  Q Q  32s 300x"
SB_SIZE = 512

LIT_ENTRY_FMT = "<Q Q I I"
LIT_ENTRY_SIZE = 24

LAB_ENTRY_FMT = "<Q I H H"
LAB_ENTRY_SIZE = 16

DIR_ENTRY_FMT = "<32s B Q Q I I I 3x"
DIR_ENTRY_SIZE = 64

VERSION_ENTRY_FMT = "<32s Q Q Q I B 7x"
VERSION_ENTRY_SIZE = 68

LEVEL_DESC_FMT = "<32s Q Q Q Q Q I I Q Q 8x"
LEVEL_DESC_SIZE = 104


class SuperBlock:
    def __init__(self, data):
        if len(data) < SB_SIZE:
            raise ValueError("SuperBlock data too short")
        
        fields = struct.unpack(SB_FMT, data)
        self.magic = fields[0]
        self.version = fields[1]
        self.total_sectors = fields[2]
        self.cluster_size = fields[3]
        self.total_clusters = fields[4]
        
        self.lit_start_cluster = fields[5]
        self.lit_clusters = fields[6]
        
        self.lab_pool_start = fields[7]
        self.lab_pool_clusters = fields[8]
        self.next_free_lab = fields[9]
        
        self.level_registry_cluster = fields[10]
        self.level_registry_clusters = fields[11]
        
        self.journal_start_cluster = fields[12]
        self.journal_sectors = fields[13]
        self.last_tx_id = fields[14]
        
        self.next_level_id = fields[15]
        self.total_levels = fields[16]
        self.root_level_id = fields[17]
        
        self.root_dir_cluster = fields[18]
        
        self.backup_sb_cluster = fields[19]
        
        self.free_cluster_hint = fields[20]
        self.total_free_clusters = fields[21]
        
        self.lat_start_cluster = fields[22]
        self.lat_sectors = fields[23]
        
        self.volume_name = fields[24].decode('utf-8', errors='ignore').strip('\x00')


class LITEntry:
    def __init__(self, data):
        self.lab_cluster, self.base_cluster, self.allocated_count, self.flags = struct.unpack(LIT_ENTRY_FMT, data)


class LABEntry:
    def __init__(self, data):
        self.next_cluster, self.level_id, self.flags, self.ref_count = struct.unpack(LAB_ENTRY_FMT, data)


class VersionEntry:
    """Level entry in root dir or folder level table"""
    def __init__(self, data):
        fields = struct.unpack(VERSION_ENTRY_FMT, data)
        self.name = fields[0].decode('utf-8', errors='ignore').strip('\x00')
        self.content_cluster = fields[1]
        self.level_id = fields[2]
        self.parent_level_id = fields[3]
        self.flags = fields[4]
        self.is_active = fields[5]


class LevelDescriptor:
    """Global level registry entry"""
    def __init__(self, data):
        fields = struct.unpack(LEVEL_DESC_FMT, data)
        self.name = fields[0].decode('utf-8', errors='ignore').strip('\x00')
        self.level_id = fields[1]
        self.parent_level_id = fields[2]
        self.root_content_cluster = fields[3]
        self.create_time = fields[4]
        self.mod_time = fields[5]
        self.flags = fields[6]
        self.ref_count = fields[7]
        self.child_count = fields[8]
        self.total_size = fields[9]


class DirEntry:
    def __init__(self, data):
        fields = struct.unpack(DIR_ENTRY_FMT, data)
        self.name = fields[0].decode('utf-8', errors='ignore').strip('\x00')
        self.type = fields[1]
        self.start_cluster = fields[2]
        self.size = fields[3]
        self.attributes = fields[4]
        self.create_time = fields[5]
        self.mod_time = fields[6]
    
    def get_type_str(self):
        types = {TYPE_FREE: "FREE", TYPE_FILE: "FILE", TYPE_LEVELED_DIR: "FOLDER",
                 TYPE_SYMLINK: "SYMLINK", TYPE_HARDLINK: "HARDLINK", TYPE_LEVEL_MOUNT: "LVLMNT"}
        return types.get(self.type, "UNKNOWN")


class FileSystem:
    def __init__(self, image_path):
        self.image_path = image_path
        self.f = None
        self.sb = None
        self.base_offset = 0

    def mount(self):
        path = self.image_path
        if os.name == 'nt':
            if len(path) == 1 and path.isalpha():
                path = f"\\\\.\\{path}:"
            elif len(path) == 2 and path[1] == ':' and path[0].isalpha():
                path = f"\\\\.\\{path}"

        if not path.startswith("\\\\.\\"):
            return False

        try:
            self.f = open(path, "rb", buffering=0)
            self.f.seek(0)
            data = self.f.read(SB_SIZE)
            if len(data) < SB_SIZE:
                return False
            self.sb = SuperBlock(data)
            if self.sb.magic != LFS_MAGIC:
                return False
            return True
        except:
            return False

    def close(self):
        if self.f:
            self.f.close()

    def read_sector(self, sector_idx, count=1):
        self.f.seek(self.base_offset + (sector_idx * SECTOR_SIZE))
        return self.f.read(count * SECTOR_SIZE)

    def read_cluster(self, cluster_idx):
        return self.read_sector(cluster_idx * SECTORS_PER_CLUSTER, SECTORS_PER_CLUSTER)

    def get_lab_entry(self, cluster):
        if cluster == 0 or cluster >= self.sb.total_clusters:
            return LABEntry(struct.pack(LAB_ENTRY_FMT, LAT_FREE, 0, 0, 0))
        
        lit_index = cluster // CLUSTERS_PER_LIT_ENTRY
        lab_offset = cluster % CLUSTERS_PER_LIT_ENTRY
        
        entries_per_lit_cluster = CLUSTER_SIZE // LIT_ENTRY_SIZE
        lit_cluster_idx = lit_index // entries_per_lit_cluster
        lit_entry_idx = lit_index % entries_per_lit_cluster
        
        lit_data = self.read_cluster(self.sb.lit_start_cluster + lit_cluster_idx)
        lit_entry_data = lit_data[lit_entry_idx * LIT_ENTRY_SIZE : (lit_entry_idx + 1) * LIT_ENTRY_SIZE]
        lit_entry = LITEntry(lit_entry_data)
        
        if lit_entry.lab_cluster == LIT_EMPTY or lit_entry.lab_cluster == 0:
            return LABEntry(struct.pack(LAB_ENTRY_FMT, LAT_FREE, 0, 0, 0))
        
        lab_data = self.read_cluster(lit_entry.lab_cluster)
        lab_entry_data = lab_data[lab_offset * LAB_ENTRY_SIZE : (lab_offset + 1) * LAB_ENTRY_SIZE]
        return LABEntry(lab_entry_data)

    def get_chain(self, start_cluster):
        chain = []
        curr = start_cluster
        while curr != 0 and curr != LAT_END and curr != LAT_BAD:
            chain.append(curr)
            entry = self.get_lab_entry(curr)
            curr = entry.next_cluster
            if len(chain) > 100000:
                break
        return chain

    def list_versions(self, cluster):
        """List VersionEntry items from a cluster (root dir or folder level table)"""
        versions = []
        chain = self.get_chain(cluster)
        for c in chain:
            data = self.read_cluster(c)
            for j in range(0, len(data), VERSION_ENTRY_SIZE):
                chunk = data[j:j+VERSION_ENTRY_SIZE]
                if len(chunk) < VERSION_ENTRY_SIZE:
                    break
                ver = VersionEntry(chunk)
                if ver.is_active and ver.name:
                    versions.append(ver)
        return versions

    def list_dir(self, content_cluster):
        """List DirEntry items from a content cluster"""
        entries = []
        chain = self.get_chain(content_cluster)
        
        for c in chain:
            data = self.read_cluster(c)
            for j in range(0, len(data), DIR_ENTRY_SIZE):
                chunk = data[j:j+DIR_ENTRY_SIZE]
                if len(chunk) < DIR_ENTRY_SIZE:
                    break
                entry = DirEntry(chunk)
                if entry.type != TYPE_FREE and entry.name:
                    entries.append(entry)
        return entries

    def list_levels(self):
        """List levels from the global level registry"""
        levels = []
        chain = self.get_chain(self.sb.level_registry_cluster)
        for c in chain:
            data = self.read_cluster(c)
            for j in range(0, len(data), LEVEL_DESC_SIZE):
                chunk = data[j:j+LEVEL_DESC_SIZE]
                if len(chunk) < LEVEL_DESC_SIZE:
                    break
                level = LevelDescriptor(chunk)
                if level.level_id != 0 and (level.flags & 0x0001):
                    levels.append(level)
        return levels

    def find_level(self, level_id):
        for level in self.list_levels():
            if level.level_id == level_id:
                return level
        return None

    def read_file(self, start_cluster, size):
        chain = self.get_chain(start_cluster)
        res = bytearray()
        remaining = size
        
        for c in chain:
            if remaining <= 0:
                break
            to_read = min(remaining, CLUSTER_SIZE)
            data = self.read_cluster(c)
            res.extend(data[:to_read])
            remaining -= to_read
            
        return bytes(res)

    def read_symlink_target(self, symlink_cluster):
        if symlink_cluster == 0:
            return None
        data = self.read_cluster(symlink_cluster)
        target = data.split(b'\x00', 1)[0].decode('utf-8', errors='ignore')
        return target
