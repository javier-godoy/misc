/*Copyright 2017 Roberto Javier Godoy.

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE. */
package ar.com.rjgodoy.commons.struts.config;

import java.io.IOException;
import java.util.ArrayList;
import java.util.Collection;
import java.util.Iterator;

import javax.servlet.jsp.JspException;
import javax.servlet.jsp.PageContext;

import org.apache.struts.Globals;
import org.apache.struts.action.ActionMessage;
import org.apache.struts.action.ActionMessages;
import org.apache.struts.taglib.TagUtils;
import org.json.JSONObject;

/**
* @author Javier Godoy
*/
public class AjaxMessageWriter {

	public static void writeErrors(PageContext pageContext) throws JspException, IOException {

		Collection<String> messages = new ArrayList<>();
		final String name = Globals.ERROR_KEY;
		final String bundle = null;
		final String locale = Globals.LOCALE_KEY;

		ActionMessages errors = TagUtils.getInstance().getActionMessages(pageContext, name);
		if ((errors == null) || errors.isEmpty()) {
			pageContext.getOut().print("{}");
			return;
		}

		for (Iterator<?> it = errors.get(); it.hasNext();) {
			ActionMessage report = (ActionMessage) it.next();
			String message;
			if (report.isResource()) {
				message = TagUtils.getInstance().message(pageContext, bundle, locale, report.getKey(), report.getValues());
			} else {
				message = report.getKey();
			}
			messages.add(message);
		}

		JSONObject obj = new JSONObject();
		obj.put("success", false);
		obj.put("messages", messages);
		pageContext.getOut().print(obj.toString());
	}

}
