//===========================================================================
//
// Name:         script.c
// Function:     script examples
// Programmer:   Mr Elusive (MrElusive@demigod.demon.nl)
// Last update:  1999-02-10
// Tab Size:     3
//===========================================================================

//*

//===========================================================================
// ask the user for a folder, the "string" message will be displayed to the
// user, the retrieved folder can be accessed in the script using the folder
// label
//===========================================================================
askfolder(folderlabel, "string");

//===========================================================================
// the "string" message is displayed to the user
//===========================================================================
message("string");

//===========================================================================
// set the output folder to the given path
//===========================================================================
outputfolder("path");

//===========================================================================
// convert a bsp file to a map file
//===========================================================================
bsp2map("pakfile":"bspfile");

//===========================================================================
// convert a bsp file to an aas file
//===========================================================================
bsp2aas("pakfile":"bspfile");

//===========================================================================
// extract files from a .pak or .sin file
//===========================================================================
extract("pakfile":"file");

//===========================================================================
// set the number of threads to use when converting
//===========================================================================
threads(num);

//===========================================================================
// execute the script with the given filename
//===========================================================================
script("filename");

//===========================================================================
// when this is found somewhere in the script BSPC will automatically
// close when all files are converted and/or extracted
//===========================================================================
exit();


//*/

//NOTE: usage of the scripted sequences below is NOT legal (read the legal
//      stuff in the winbspc.htm)
//      they are only listed here as script examples

message("This script will extract all your Quake2 textures");
askfolder(quake2folder, "Please enter your Quake2 folder");
askfolder(outputfolder, "Please enter the folder to store the textures");
outputfolder(outputfolder);
extract(quake2folder"baseq2\\pak?.pak":"*.wal");

message("This script will convert all your Quake2 BSP files to MAP files");
askfolder(quake2folder, "Please enter your Quake2 folder");
askfolder(outputfolder, "Please enter the folder to store the MAP files");
outputfolder(outputfolder);
bsp2map(quake2folder"baseq2\\pak?.pak":"maps/*.bsp");

message("This script will convert all your Quake1 BSP files to MAP files");
askfolder(quake1folder, "Please enter your Quake1 folder");
askfolder(outputfolder, "Please enter the folder to store the MAP files");
outputfolder(outputfolder);
bsp2map(quake1folder"id1\\pak?.pak":"maps/*.bsp");

message("This script will convert all your Half-Life BSP files to MAP files");
AskFolder(halflifefolder, "Please enter your Half-Life folder");
AskFolder(outputfolder, "Please enter the folder to store the MAP files");
outputfolder(outputfolder);
bsp2map(halflifefolder"valve\\pak0.pak":"maps/*.bsp");

message("This script will convert all your Sin BSP files to MAP files");
askfolder(sinfolder, "Please enter your Sin folder");
askfolder(outputfolder, "Please enter the folder to store the MAP files");
outputfolder(outputfolder);
bsp2map(sinfolder"base\\pak?.sin":"maps/*.bsp");

//*/

