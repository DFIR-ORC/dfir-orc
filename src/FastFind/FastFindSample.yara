import "pe"
import "hash"
/*
This is a simple string lookup rule ...
*/
rule simple_string{
    strings:
        $text_string = "HelloWorld"
    condition :
        $text_string
}
rule single_section
{
condition:
    pe.number_of_sections == 1
}

rule control_panel_applet
{
condition:
    pe.exports("CPlApplet")
}

rule is_dll
{
condition:
    pe.characteristics & pe.DLL
}

rule is_msrt_exe
{
strings:
	$random_data = {B8 BA 75 A2 2D 27 E7 00 32 7E 01 15 5E A8 7D 16} 
	/* $random_data = {FF BA 75 A2 2D 27 E7 00 32 7E 01 15 5E A8 7D 16} */
condition:
	$random_data and pe.EXECUTABLE_IMAGE
}
