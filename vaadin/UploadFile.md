```java
/**
 *   Copyright (C) 2022 Roberto Javier Godoy
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
   


	private void updateState(FileInfo.FileInfoBuilder builder) {
		updateState(builder.build());
	}

	private void updateState(FileInfo info) {
		StringBuilder sb = new StringBuilder();

		sb.append("var d = this;");
		sb.append("var i = d.files.findIndex(f=>f.name==$0.name);");
		sb.append("if (i<0) return;");
		sb.append("var f = d.shadowRoot.querySelectorAll('vaadin-upload-file')[i];");
		sb.append("if (d.files.some((e,j)=>e.name==$0.name && j>i)) d.files=d.files.filter((e,j)=>e.name!=$0.name || j<=i);");
		sb.append("f.file = Object.assign({}, $0);");

		//sb.append("d.files[i] = "+json.toJson()+";");
		//sb.append("var j = d.files.length; while (--j>i) if (d.files[i].name==d.files[j].name) d.files.splice(j, 1);");
		//sb.append("d.files = Array.from(d.files);");

		getUI().ifPresent(ui->ui.access(()->upload.getElement().executeJs(sb.toString(), info.toJson())));
	}
	
	@Builder
	@AllArgsConstructor(access = AccessLevel.PRIVATE)
	private static class FileInfo {

		private String name;

		@Accessors(fluent = true)
		public static class FileInfoBuilder {
			@Setter(value = AccessLevel.PRIVATE)
			private String name;

			@Setter
			private Boolean complete;

			@Setter
			private Boolean indeterminate;

			private FileInfoBuilder(String name) {
				this.name = name;
			}

			public FileInfoBuilder complete() { complete(true); return this; }
		    public FileInfoBuilder indeterminate() { indeterminate(true); return this;}
		}

		public static FileInfoBuilder builder(String filename) {
			return new FileInfoBuilder(filename);
		}

		public static FileInfoBuilder builder(FinishedEvent ev) {
			return builder(ev.getFileName());
		}

		public JsonObject toJson() {
			JsonObject json = Json.createObject();
			json.put("name", name);
			if (complete!=null) {
				json.put("complete", complete);
			}
			if (errorMessage!=null) {
				json.put("error", errorMessage);
			}
			if (held!=null) {
				json.put("held", held);
			}
			if (indeterminate!=null) {
				json.put("indeterminate", indeterminate);
			}
			if (progress!=null) {
				json.put("progress", progress);
			}
			if (status!=null) {
				json.put("status", status);
			}
			if (uploading!=null) {
				json.put("uploading", uploading);
			}
			return json;
		}

		  /**
	       * True if uploading is completed, false otherwise.
	       */
	      Boolean complete;

	      /**
	       * Error message returned by the server, if any.
	       */
	      String errorMessage;

	      /**
	       * True if uploading is not started, false otherwise.
	       */
	      Boolean held;

	      /**
	       * True if remaining time is unknown, false otherwise.
	       */
	      Boolean indeterminate;

	      /**
	       * Number representing the uploading progress.
	       */
	      Integer progress;

	      /**
	       * Uploading status message.
	       */
	      String status;

	      /**
	       * True if uploading is in progress, false otherwise.
	       */
	      Boolean uploading;
	}
```
