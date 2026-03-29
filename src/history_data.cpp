#include "history_data.h"

namespace hd {
void init() {
	{
		scoped_lock meter_lock{meter_mutex};
		g::meter_data = {};
	}
}

}

