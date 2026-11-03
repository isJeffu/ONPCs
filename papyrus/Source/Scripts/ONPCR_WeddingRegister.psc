Scriptname ONPCR_WeddingRegister extends ObjectReference
{OStim NPCs Reforged - the wedding register in the Temple of Mara: activating it shows the upcoming weddings and their dates.
0.17.2: in pages of 8 - when there is more than one page, a prompt (Next page / Previous page / Close) follows each page.}

Event OnActivate(ObjectReference akActionRef)
	if akActionRef != Game.GetPlayer()
		return
	endif
	int pages = ONPCR_Ladder.GetWeddingRegisterPages()
	Message nav = None
	if pages > 1
		nav = Game.GetFormFromFile(0x0E09, "OStimNPCsReforged - Letters.esp") as Message	; ONPCR_WeddingRegisterNav
	endif
	int page = 1
	bool reading = true
	while reading
		Debug.MessageBox(ONPCR_Ladder.GetWeddingRegisterPage(page))
		if pages <= 1 || !nav
			reading = false
		else
			Utility.Wait(0.1)	; game time does not run while the box is open, so this returns once it is closed
			int choice = nav.Show(page as float, pages as float)
			if choice == 0
				page += 1
				if page > pages
					page = 1
				endif
			elseif choice == 1
				page -= 1
				if page < 1
					page = pages
				endif
			else
				reading = false
			endif
		endif
	endwhile
EndEvent
