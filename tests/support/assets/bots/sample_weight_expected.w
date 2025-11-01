
weight "single_value"
{
	 return 42;
} //end weight

weight "switch_tree"
{
	switch(3)
	{
		case 1: return balance(1.5,0.5,2.5);
		case 2:
		{
			switch(7)
			{
				case 4: return 4;
				default: return 5;
			} //end switch
		} //end case
		default: return 9;
	} //end switch
} //end weight
