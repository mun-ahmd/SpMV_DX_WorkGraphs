// ---- push constants (root constants) -> InitAsConstants(6, 0) ----
cbuffer Meta : register(b0)
{
    uint NumRows;         // number of matrix rows
    uint NumNonZeros;     // number of non-zero entries
    uint NumCols;     // length of input/output vector (usually NumCols)
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


struct EntryRecord
{
    uint3  dispatchGrid : SV_DispatchGrid;
};


struct CSRRow{
    uint rowId;
    uint rowPtr;
    uint numValues;
};

//below sets a theoretical limit on the number of rows this can process
//  limit should be max dispatch grid size * 4
#define MAX_ROWS_PER_THREAD 4

[Shader("node")]
[NodeIsProgramEntry]
[NodeLaunch("broadcasting")]
[NodeMaxDispatchGrid(32, 1, 1)]
[NumThreads(32, 1, 1)]
[NodeId("Entry", 0)]
void EntryFunction(
    DispatchNodeInputRecord<EntryRecord> inputRecord,

    uint3 dispatchThreadId : SV_DispatchThreadID,
    
    [MaxRecords(32 * MAX_ROWS_PER_THREAD)]
    [NodeId("MultiplyRow")]
    NodeOutput<CSRRow> csrRowOutput
)
{
    uint dispatchGrid = inputRecord.Get().dispatchGrid.x;
    
    //Thread processes nothing if there are too many, otherwise it is grid parallel
    const uint numThreadOutputs = (dispatchThreadId.x >= NumRows) ? 0 : (1 + (
        max(NumRows - dispatchThreadId.x - 1, 0) / (dispatchGrid * 32)
    ));
    //if(dispatchThreadId.x < NumRows){
    //    OutVector[dispatchThreadId.x] = float(numThreadOutputs);
    //}
    //return;
    ThreadNodeOutputRecords<CSRRow> multiplyOutputs = 
        csrRowOutput.GetThreadNodeOutputRecords(
            numThreadOutputs
        );

    //one thread per few rows (ideally)
    //grid parallel loop
    for(uint outputIdx = 0; outputIdx < numThreadOutputs; outputIdx++){
        uint rowIdx = dispatchThreadId.x + (outputIdx * dispatchGrid * 32);
   //version 1: Launch the MultiplyRow for every row
        uint rowPtr = RowPtr[rowIdx];
        uint numValuesInRow = RowPtr[rowIdx + 1] - rowPtr;

        multiplyOutputs.Get(outputIdx).rowId = rowIdx;
        multiplyOutputs.Get(outputIdx).rowPtr = rowPtr;
        multiplyOutputs.Get(outputIdx).numValues = numValuesInRow;

        //version 2: Launch the MultiplyRow for every long row
        //todo
    }
    
    //while this is okay, it might be worth it to figure out
    //  how to call output complete every iteration
    //      why? -> so that the multiplies can be scheduled earlier instead of being serial pretty much
    //          okay they are not exactly serial in this case either due to oversubscription
    //              the tail effect should be minimized in this case too
    multiplyOutputs.OutputComplete();
    return;
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