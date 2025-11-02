//===========================================================================
//
// Name:			script.c
// Function:
// Programmer:		Mr Elusive (MrElusive@idsoftware.com)
// Last update:		1999-08-14
// Tab Size:		3 (real tabs)
//===========================================================================


//script procedure (script with the name "main" is initial script)
script "name" {}
//point definition (default positions "entity1", "entity2" etc.)
point("name", x, y, z);
//box definition
box("name", minx, miny, minz, maxx, maxy, maxz);
//move a box
movebox("boxname", "pointname");
//move bot to a certain position
moveto("boxname");
//aim at a certain point
aim("point");
//say something
say("text", "wav filename");
//do a specific model animation
wave("wave type");
//select weapon with number 'x'
selectweapon(x);
//fire the current weapon
fireweapon();
//wait 'x' seconds
wait(time(x));
//wait until entity with number 'entitynum' touches 'box'
wait(touch(entitynum, "box"));

script "main"
{
	point("red armor point", 120, 120, 120);
	box("red armor box", -16, -16, -16, 16, 16, 16);
	movebox("red armor box", "red armor point");
	//
	point("near red armor point", 100, 100, 100);
	box("near red armor box", -16, -16, -16, 16, 16, 16);
	movebox("near red armor box", "near red armor point");
	//
	say("I'm going to the red armor", NULL);
	say("follow me", NULL);
	moveto("red armor box");
	wait(touch(1, "red armor box"));
	moveto("near red armor box");
	wait(touch(0, "red armor box");
}

