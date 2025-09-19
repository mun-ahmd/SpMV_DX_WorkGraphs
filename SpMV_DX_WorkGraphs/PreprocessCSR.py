import scipy.io
import numpy as np
import struct
from pathlib import Path

MAT_DIR = Path('./matrices')

def convert_mat(mat_path : str, csrbin_path : str):
    # Load MATLAB .mat file
    mat = scipy.io.loadmat(mat_path)
    
    # It is highly likely that the path to the matrix is different for different .mat files
    A = mat['Problem'][0][0][1].tocsr()   # ensure CSR format

    # pack with a random vector and result for correctness checks
    random_vector = np.random.rand(A.shape[1])
    mult_result = A @ random_vector

    # Write to binary file
    with open(csrbin_path, "wb") as f:
        # Write dimensions
        f.write(struct.pack("ii", A.shape[0], A.shape[1]))
        # Write nnz
        f.write(struct.pack("i", A.nnz))
        
        # Write indptr
        f.write(struct.pack(f"{len(A.indptr)}i", *A.indptr))
        # Write indices
        f.write(struct.pack(f"{len(A.indices)}i", *A.indices))
        # Write data (as float)
        f.write(struct.pack(f"{len(A.data)}f", *A.data))

        # Write random_vector (as float)
        f.write(struct.pack(f"{len(random_vector)}f", *random_vector))
        # Write mult_result (as float)
        f.write(struct.pack(f"{len(mult_result)}f", *mult_result))

if __name__ == '__main__':
    mats = list(MAT_DIR.glob('*.mat'))
    csrbins = list(MAT_DIR.glob('*.csrbin'))
    print(mats, csrbins)
    for mat in mats:
        csrbin_path = mat.with_suffix('.csrbin')
        
        if csrbin_path in csrbins:
            choice = input(f'While converting {mat.name} found file {csrbin_path.name} already exists at {csrbin_path}\nEnter y to overwrite existing...')
            if choice.lower().strip() not in ['y', 'yes']:
                continue

        convert_mat(str(mat), str(csrbin_path))

