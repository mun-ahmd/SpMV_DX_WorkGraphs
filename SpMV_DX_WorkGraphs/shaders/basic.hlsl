// ---- push constants (root constants) -> InitAsConstants(6, 0) ----
cbuffer Meta : register(b0)
{
    uint NumRows;         // number of matrix rows
    uint NumNonZeros;     // number of non-zero entries
    uint VectorCount;     // length of input/output vector (usually NumRows)
    uint DispatchX;       // optional dispatch/work grouping info
    uint Dummy4;          // padding / reserved
    uint Dummy5;          // padding / reserved
};

// ---- read-only CSR arrays (SRVs) ----
// RowPtr has (NumRows + 1) entries of uint
StructuredBuffer<uint> RowPtr   : register(t0);

// ColIdx has NumNonZeros entries of uint
StructuredBuffer<uint> ColIdx   : register(t1);

// Values has NumNonZeros entries of float
StructuredBuffer<float> Values  : register(t2);

// Dense input vector (read-only)
StructuredBuffer<float> InVector : register(t3);

// ---- writeable outputs (UAVs) ----
// Output vector (one float per row)
RWStructuredBuffer<float> OutVector      : register(u0);

//// Optional: writable color backbuffer (if you write a texture)
//RWTexture2D<float4>       WritableBackbuffer : register(u1);

//// If you have additional UAVs, use u2, u3, ...
//RWStructuredBuffer<uint>  DebugCounter    : register(u2); // example

struct EntryRecord
{
    uint  dispatchGrid : SV_DispatchGrid;
};

struct CSRRow{
    uint rowId;
    uint rowPtr;
    uint numValues;
};


[Shader("node")]
[NodeIsProgramEntry]
[NodeLaunch("broadcasting")]
[NodeMaxDispatchGrid(1024, 1, 1)]
[NumThreads(32, 1, 1)]
[NodeId("Entry", 0)]
void EntryFunction(
    DispatchNodeInputRecord<EntryRecord> inputRecord,

    uint3 dispatchThreadId : SV_DispatchThreadID,
    
    [MaxRecords(1)]
    [NodeId("MultiplyRow")]
    NodeOutput<CSRRow> csrRowOutput
)
{
    //one thread per row (ideally)
    for(uint rowIdx = dispatchThreadId.x; rowIdx < NumRows; rowIdx += (inputRecord.Get().dispatchGrid * 32)){
        //grid parallel for loop -> needs adjustment for work graphs
        //Warning: Since the MaxRecords is currently 1, it will fail for second iteration
        //Warning: Since these launch ops are required to be run by every thread together, it could fail in case of a second iteration: So this is really unsafe code

        //version 1: Launch the MultiplyRow for every row
        uint rowPtr = RowPtr[rowIdx];
        //note this assumes that rowIdx will max out at NumRows - 1
        //  should you just read both values into shared memory
        //      I think that is over optimization
        uint numValuesInRow = RowPtr[rowIdx + 1] - rowPtr;

        ThreadNodeOutputRecords<CSRRow> multiplyOutputs =
        csrRowOutput.GetThreadNodeOutputRecords(1);
        multiplyOutputs.Get().rowId = rowIdx;
        multiplyOutputs.Get().rowPtr = rowPtr;
        multiplyOutputs.Get().numValues = numValuesInRow;
        multiplyOutputs.OutputComplete();

        //version 2: Launch the MultiplyRow for every long row
        //todo

    }
}

groupshared float mult_results[32];
[Shader("node")]
[NodeLaunch("coalescing")]
[NumThreads(32, 1, 1)]
[NodeId("MultiplyRow")]
void MultiplyRow(
    uint3 localIdx : SV_GroupThreadID,
    [MaxRecords(1)]
    GroupNodeInputRecords<CSRRow> inputRecords
){
    //this is a terminal node (no launches from here)
    //I should make it chunked since one mult per thread is so little
    mult_results[localIdx.x] = 0.f;
    for(
        uint element_id = localIdx.x;
        element_id < inputRecords.Get(0).numValues;
        element_id += 32
    ){
        uint col = ColIdx[inputRecords.Get(0).rowPtr + element_id];
        float vec_val = InVector[col];
        float val = Values[inputRecords.Get(0).rowPtr + element_id];
        
        mult_results[localIdx.x] += (vec_val * val);
    }
    GroupMemoryBarrierWithGroupSync();
    if(localIdx.x == 0){
        //world's no 1 reduction for sure
        float acc = 0.f;
        for(uint j = 0; j < 32; j++){
            acc += mult_results[j];
        }
        OutVector[inputRecords.Get(0).rowId] = acc;
    }
}