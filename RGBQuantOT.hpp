#pragma once

/*
  Copyright (c) 2018 Victor Sheinmann, Vicshann@gmail.com
  Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), 
  to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, 
  and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
  The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, 
  WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE. 
*/

// TODO: Optimize memory
//------------------------------------------------------------------------------------------------------------------------------
class CRGBOTQuantizer     // OCTREEQUANTIZER
{
public:
static const int TREEMINDEPTH = 2;  // Why 2 is min?
static const int TREEMAXDEPTH = 9;
static const int DEFAULTDEPTH = 5;    // Most balanced quality  // But very SLOW!
static const int TOTALCOLORS  = 256;
static const int COLRANGEMAX  = TOTALCOLORS-1;

union RGBQUAD 
{
 unsigned int Value;
 struct
  {
   unsigned char Blue;
   unsigned char Green;
   unsigned char Red;
   unsigned char Unused;
  };
};
private:
//------------------------------------------------------------------------------------------------------------------------------
struct COLORVALUE
{
 unsigned int ClrValArrayR[TOTALCOLORS*2];   // 'short' is not enough
 unsigned int ClrValArrayG[TOTALCOLORS*2];
 unsigned int ClrValArrayB[TOTALCOLORS*2];
 unsigned int *CVAMiddleR;
 unsigned int *CVAMiddleG;
 unsigned int *CVAMiddleB;
};
//------------------------------------------------------------------------------------------------------------------------------
struct FPQINDEX      // First Pass Quantize color index table
{
 unsigned int ArrayB[TOTALCOLORS];
 unsigned int ArrayG[TOTALCOLORS];
 unsigned int ArrayR[TOTALCOLORS];
};
//------------------------------------------------------------------------------------------------------------------------------
struct COLORNODE     // Represents positions of nodes with colors (For first - all colors, mapped to BASE level, then redused number of colors(nodes) on different tree levels)
{
 unsigned int NodeLevel;    // Level of node with color in octree
 unsigned int NodeIndex;    // Index of node with color in octree on 'NodeLevel' level
};
//------------------------------------------------------------------------------------------------------------------------------
struct OCTREENODE    // Represents each node in octree (24 bytes)
{
 unsigned int  ValueR;       // Weight of R
 unsigned int  ValueG;       // Weight of G
 unsigned int  ValueB;       // Weight of B
 unsigned int  CurPixNum;    // Number of pixels, mapped to this node
 unsigned int  ChiPixNum;    // Number of pixels, mapped to all child nodes of this node
 unsigned int  ChiNodesMask; // Mask of used child nodes (D0-D7, Foe each node if bit is set - node is not empty)   // 'char' is enough but 'int' is used to keep alignment
// unsigned short  PalIndex;     // Index in palette where color value of this node was placed
// unsigned char   FDirty;       // Use 'ChiPixNum' instead; Checking each node on 'AppendColor' and 'CreateTreeNodes' also makes the code slower
};                   // When Data alignment=4, at end 3 bytes after 'ChiNodesMask' is free
//------------------------------------------------------------------------------------------------------------------------------


private:
   int LastNColor;      // To speed up FindNearestColor a little
   int LastNClrIdx;     // To speed up FindNearestColor a little
   unsigned int MaxNodes;
   unsigned int PalColorIndex;
   COLORNODE*   ColorNodes;
   FPQINDEX     FPQTable;
   COLORVALUE   ClrValTable;

public:
   unsigned int TreeDepth;
   unsigned int ColorsInTree;
   RGBQUAD*     CurPalette;
   OCTREENODE*  NodeLevels[TREEMAXDEPTH];

private:
//==============================================================================================================================
// Count nodes with pixels
// We using a 6-LEVEL tree:
//
//               |
//              |||
//             |||||
//            |||||||
//           |||||||||
//  BASE:   ||||||||||| - Colors was mapped here
//
// If use full tree depth (8) size of tree = 48 MB  => 2097152 Nodes
// Set for each node number of mapped pixels in all his child nodes
// ColorNodes counts only BASE nodes with min 1 pixels mapped
// Tree builds DOWN -> TOP
//------------------------------------------------------------------------------------------------------------------------------
void CreateTreeNodes(void)
{
 for(unsigned int nctr=0;nctr < MaxNodes;nctr++)
  {
   OCTREENODE* ONode = &NodeLevels[TreeDepth][nctr];
   if(ONode->CurPixNum)
    {                                             // Current node counts minimum 1 pixel
     ColorsInTree++;                              // Count of total used colors ( <= 32768)
     COLORNODE* ClrNode = &ColorNodes[ColorsInTree];
     ClrNode->NodeLevel = TreeDepth;    // Depth Max (0 - (TreeDepth-1)), initially each node assumed depth is BASE
     ClrNode->NodeIndex = nctr;         // Base Node Index
     ONode->ChiPixNum   = ONode->CurPixNum; // Set ChildPixelsCtr To CurPixelsCtr for base node with no child nodes
     // If node not empty, set for his parents on all tree levels, bit ChildNMask so all child will bound one by one
     for(int cnode=(TreeDepth-1),index=nctr;cnode >= 0;cnode--) // Build child for parent nodes from BASE node with color (Adds one child node per level on each pass)
      {
       unsigned int ChildNMask = (1 << (index & 7));  // Use index in BASE level as Low_8 index of child in parent
       index >>= 3;   // '>> 3' is '/ 8'              // For each next level, nodes count = (CurLevelNodes / 8)
       OCTREENODE* ONodeD = &NodeLevels[cnode][index];
       ONodeD->ChiPixNum    += ONode->CurPixNum; // Each node contains counter(Sum) of pixels in all his child nodes
       ONodeD->ChiNodesMask |= ChildNMask;  // Mask of Child nodes (For each NotEmpty BASE nodes, allk parents have Bit is Set)
      }
    }
  }
 for(unsigned int cind=ColorsInTree;cind > 0;cind--)this->UpdateColorNodes(cind); // Set ColorNodes to Octree nodes with approriate color values
}
//------------------------------------------------------------------------------------------------------------------------------
void UpdateColorNodes(unsigned int ColorIndex)
{
 unsigned int PrvNLevel = ColorNodes[ColorIndex].NodeLevel;
 unsigned int PrvNIndex = ColorNodes[ColorIndex].NodeIndex;
 unsigned int PrvNCPixN = NodeLevels[PrvNLevel][PrvNIndex].ChiPixNum;

 for(unsigned int MaxClrIdx = (ColorsInTree >> 1);ColorIndex <= MaxClrIdx;)     // '/ 2'
  {
   unsigned int NStepCInd = (ColorIndex << 1);  // '* 2'
   COLORNODE* ClrNode = &ColorNodes[NStepCInd];
   if(NStepCInd < ColorsInTree)
    {
     unsigned int AVal = (bool)(NodeLevels[ ClrNode->NodeLevel ][ ClrNode->NodeIndex ].ChiPixNum > NodeLevels[ ClrNode[1].NodeLevel ][ ClrNode[1].NodeIndex ].ChiPixNum);
     NStepCInd += AVal;
     ClrNode   += AVal;
    }
   if(PrvNCPixN <= NodeLevels[ ClrNode->NodeLevel ][ ClrNode->NodeIndex ].ChiPixNum)break;
   ColorNodes[ColorIndex] = *ClrNode; //  ColorNodes[NStepCInd];   // Copy full SARRAY struct   
   ColorIndex = NStepCInd;
  } 
 ColorNodes[ColorIndex].NodeLevel = PrvNLevel;
 ColorNodes[ColorIndex].NodeIndex = PrvNIndex;  
}
//------------------------------------------------------------------------------------------------------------------------------
void ReduceColorsInTree(unsigned int MaxColors)
{
 COLORNODE* ClrNodeOne = &ColorNodes[1];
 while(ColorsInTree > MaxColors)
  {
   unsigned int FCNIndex = ClrNodeOne->NodeIndex;
   unsigned int FCNLevel = (ClrNodeOne->NodeLevel - (bool)ClrNodeOne->NodeLevel);
   OCTREENODE*  SrcNode  = &NodeLevels[ ClrNodeOne->NodeLevel ][FCNIndex];
   OCTREENODE*  DstNode  = &NodeLevels[FCNLevel][ FCNIndex >> 3 ];  // '/ 8'

   if(DstNode->CurPixNum != 0)
    {
     ColorNodes[1] = ColorNodes[ColorsInTree];   // Full node struct copy
     ColorsInTree--;
    }
     else
      {
       ClrNodeOne->NodeLevel = FCNLevel;
       ClrNodeOne->NodeIndex = (FCNIndex >> 3);  // '/ 8'
      }
   DstNode->CurPixNum    += SrcNode->CurPixNum;
   DstNode->ValueR       += SrcNode->ValueR;
   DstNode->ValueG       += SrcNode->ValueG;
   DstNode->ValueB       += SrcNode->ValueB;
   DstNode->ChiNodesMask &= ~(1 << (FCNIndex & 7));   // (2 pow (TmpCtr & 0x07))
   this->UpdateColorNodes(1);
  } 
}
//------------------------------------------------------------------------------------------------------------------------------
void MakePaletteFromTree(unsigned int Level, unsigned int Index)
{
 OCTREENODE*  Node = &NodeLevels[Level][Index];
 if(Node->ChiNodesMask != 0)  // If current node have any child nodes{node is not leaf} - Go recursion trough them first
  {
   for(int ctr=7;ctr >= 0;ctr--)  // Check each of 8 child nodes
    {
     if((Node->ChiNodesMask & (1 << ctr)))this->MakePaletteFromTree((Level+1),(ctr+(Index << 3)));  // '<< 3' is '* 8'  // Go to recursion if Bit of child node in mask is Set ((Index*8) Group of child nodes of current node on next tree level; 'ctr' used as index of child node in group)
    }
  }
 if(Node->CurPixNum != 0)  // If node contains color info, create a palette entry from it
  {
   RGBQUAD* PalEntry = &CurPalette[PalColorIndex];
   PalEntry->Red   = ((Node->ValueR + (Node->CurPixNum >> 1)) / Node->CurPixNum); // Create value of RED   from WeightR and PixCount   // '>> 1' is '/ 2'
   PalEntry->Green = ((Node->ValueG + (Node->CurPixNum >> 1)) / Node->CurPixNum); // Create value of GREEN from WeightG and PixCount
   PalEntry->Blue  = ((Node->ValueB + (Node->CurPixNum >> 1)) / Node->CurPixNum); // Create value of BLUE  from WeightB and PixCount
  // Node->PalIndex = PalColorIndex;     // Index in palette where color value of this node was placed: This value is only for statistic
   PalColorIndex++;        // Increase palette entry counter
  }
}

public:
//------------------------------------------------------------------------------------------------------------------------------
int GetPalette(RGBQUAD *Palette, int MaxColors)
{
 if(CurPalette){for(int ctr=0,Nodes;ctr < TREEMAXDEPTH;ctr++,Nodes=(Nodes*8))memset(NodeLevels[ctr], 0, sizeof(OCTREENODE)*Nodes);}  // If Tree nodes has been already used, clear nodes; (After VirtualAlloc memory is ZERO) - Slow, maybe better use a DIRTY flag of tree node ?

 CurPalette    = Palette;
 ColorsInTree  = 0;
 PalColorIndex = 0;

 this->CreateTreeNodes();
 this->ReduceColorsInTree(MaxColors);
 this->MakePaletteFromTree(0,0);
 return PalColorIndex;
}
//------------------------------------------------------------------------------------------------------------------------------
// Because we do not use full depth octree and do not store colors in nodes by
// their RGB indexes, we cannot search nearest colors in tree by requested RGB
//
int FindNearestColor(unsigned char Red, unsigned char Green, unsigned char Blue)
{
 int ColorCode = (Red << 16)|(Green << 8)|Blue;
 if(ColorCode == this->LastNColor)return this->LastNClrIdx;    // TODO: Some tree to cache all found colors

 int Index = -1;
 unsigned int MaxValue = -1;  // Low Color trim data loss constant
 for(unsigned int ctr=0;ctr < ColorsInTree;ctr++)
  {
   RGBQUAD* PalEntry  = &CurPalette[ctr];
   unsigned int Value = ClrValTable.CVAMiddleR[ PalEntry->Red - Red ] + ClrValTable.CVAMiddleG[ PalEntry->Green - Green ] + ClrValTable.CVAMiddleB[ PalEntry->Blue - Blue ];
   if(Value < MaxValue){MaxValue = Value; Index = ctr;}
  }
 this->LastNColor  = ColorCode;
 this->LastNClrIdx = Index;
 return Index;
}
//------------------------------------------------------------------------------------------------------------------------------
// If Depth = 0 - Releases all used memory
// VirtualAlloc is faster than HeapAlloc ???
// SetOctreeDepth mainly affects to First Pass Quantization
//
void SetOctreeDepth(int Depth)
{
 int Nodes = 0;
 if(Depth > TREEMAXDEPTH)Depth = TREEMAXDEPTH;
   else if(Depth < TREEMINDEPTH)Depth = TREEMINDEPTH;
 if((unsigned int)Depth == (TreeDepth+1))return;      // Do not change anything !!!
 MaxNodes  = 0;

 for(int ctr=0,nctr=1;ctr < TREEMAXDEPTH;ctr++,nctr*=8)
  {
   if(ctr  <  Depth)Nodes = nctr;                        // TODO: Optimize - rearrange
   if((ctr <  Depth)&&(NodeLevels[ctr] == nullptr)){NodeLevels[ctr] = new OCTREENODE[nctr]();}   // Specifying '()' for POD allocation forces a compiler to set that memory to 0 (C++ spec); MSVC invokes memset for this when SPEED optimization is enabled or uses 'rep stos' without it. Also with SPEED optimization it uses malloc instead of global 'new'        // (OCTREENODE*)VirtualAlloc(NULL,(sizeof(OCTREENODE)*nctr),MEM_COMMIT,PAGE_READWRITE);
   if((ctr >= Depth)&&(NodeLevels[ctr] != nullptr)){delete[] NodeLevels[ctr]; NodeLevels[ctr] = nullptr;}    // VirtualFree(NodeLevels[ctr],NULL,MEM_RELEASE);
  }

 TreeDepth = Depth;
 if(ColorNodes){delete[] ColorNodes; ColorNodes = nullptr;}   //  VirtualFree(ColorNodes,NULL,MEM_RELEASE);
 if(!TreeDepth)return;       // Exit, if Depth = 0

 TreeDepth--;
 MaxNodes   = Nodes;
 ColorNodes = new COLORNODE[Nodes+1]();        //(COLORNODE*)VirtualAlloc(NULL,(sizeof(COLORNODE)*(Nodes+1)),MEM_COMMIT,PAGE_READWRITE);

 // Create a first pass color quantization table
 // Makes adding colors to Octree much more faster (Nodes indexing by combined colors)
 // For max (16777216) colors if Depth = 9
 // Mask Trimmed to tree depth
 // This Is BEST WAY ???
 //
 // ArrayB[Counter] = Counter => D7-D6-D5-D4-D3-D2-D1-D0 => D7-0-0-D6-0-0-D5-0-0-D4-0-0-D3-0-0-D2-0-0-D1-0-0-D0
 // ArrayG[Counter] = Counter => D7-D6-D5-D4-D3-D2-D1-D0 => D7-0-0-D6-0-0-D5-0-0-D4-0-0-D3-0-0-D2-0-0-D1-0-0-D0-0
 // ArrayR[Counter] = Counter => D7-D6-D5-D4-D3-D2-D1-D0 => D7-0-0-D6-0-0-D5-0-0-D4-0-0-D3-0-0-D2-0-0-D1-0-0-D0-0-0
 //
 for(unsigned int Counter=0,Mask=0;Counter < 256;Counter++)
  {
   Mask = (((Counter & 0x80) << 14)|((Counter & 0x40)<<12)|((Counter & 0x20)<<10)|((Counter & 0x10)<<8)|((Counter & 0x08)<<6)|((Counter & 0x04)<<4)|((Counter & 0x02)<<2)|(Counter & 0x01));
   Mask = (Mask >> ((TREEMAXDEPTH - Depth) * 3)); // Trim Max Colors to Tree Depth
   FPQTable.ArrayR[Counter] = (FPQTable.ArrayG[Counter] = (FPQTable.ArrayB[Counter] = Mask) << 1) << 1;
  }
}
//------------------------------------------------------------------------------------------------------------------------------
void AppendColor(unsigned char Red, unsigned char Green, unsigned char Blue)
{
 unsigned int NodeIndex = (FPQTable.ArrayB[ Blue ] + FPQTable.ArrayG[ Green ] + FPQTable.ArrayR[ Red ]);  // Get quantized index of node, some different colors will be mapped as same; NodeIndex will be optimized to REGISTER variable
 OCTREENODE*  Node = &NodeLevels[TreeDepth][NodeIndex];
 Node->ValueR += Red;    // Update weight of RED           // TODO: Need to check for overflow? // (4096 x 4096) x 255 (of same color) is still fits into UINT32
 Node->ValueG += Green;  // Update weight of GREEN
 Node->ValueB += Blue;   // Update weight of BLUE
 Node->CurPixNum++;      // Update mapped pixels counter for node
}
//------------------------------------------------------------------------------------------------------------------------------
CRGBOTQuantizer(void)
{
 LastNColor    = -1;
 LastNClrIdx   = -1;
 CurPalette    = nullptr;
 MaxNodes      = 0;
 ColorNodes    = nullptr;
 TreeDepth     = -1;
 ColorsInTree  = 0;
 PalColorIndex = 0;

 for(int ctr=0;ctr < TREEMAXDEPTH;ctr++)NodeLevels[ctr] = nullptr;
 for(int ctr=-COLRANGEMAX,index=0;ctr <= COLRANGEMAX;ctr++,index++) // Precalculate ccolor values (fi = 30*(Ri-R0)2+59*(Gi-G0)2+11*(Bi-B0)2)   // Range: -255 <> +255
  {
   ClrValTable.ClrValArrayR[index] = (ctr*ctr)*32;    // 30 // The Numbers are human eye color perception differences
   ClrValTable.ClrValArrayG[index] = (ctr*ctr)*64;    // 59
   ClrValTable.ClrValArrayB[index] = (ctr*ctr)*16;    // 11
  }
 ClrValTable.CVAMiddleR = &ClrValTable.ClrValArrayR[COLRANGEMAX];  // Or 256(Second half)???????
 ClrValTable.CVAMiddleG = &ClrValTable.ClrValArrayG[COLRANGEMAX];
 ClrValTable.CVAMiddleB = &ClrValTable.ClrValArrayB[COLRANGEMAX];
// this->SetOctreeDepth(DEFAULTDEPTH);
}
//------------------------------------------------------------------------------------------------------------------------------
~CRGBOTQuantizer()
{
 this->SetOctreeDepth(0);
}
//------------------------------------------------------------------------------------------------------------------------------
};
//------------------------------------------------------------------------------------------------------------------------------
