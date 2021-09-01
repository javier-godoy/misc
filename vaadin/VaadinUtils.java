/**
Copyright (C) 2018 - 2021 Roberto Javier Godoy

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU Affero General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Affero General Public License for more details.

You should have received a copy of the GNU Affero General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

public static class VaadinUtils {

	public static <T> T __(T t, Consumer<T> consumer) {
		consumer.accept(t);
		return t;
	}

	/**
	 * Returns a composed function that first applies the {@code this_} function to
	 * its input, and then applies the {@code after} function to the result. If
	 * evaluation of either function throws an exception, it is relayed to the
	 * caller of the composed function.
	 *
	 * @param <R>   the type of the result of the {@code this_} function and of the
	 *              input to the {@code after} function.
	 * @param <T>   the type of the input to the {@code this_} function and to the
	 *              composed function
	 * @param <V>   the type of output of the {@code after} function, and of the
	 *              composed function
	 * @param this_ the function to apply before the {@code after} function is
	 *              applied
	 * @param after the function to apply after the {@code this_} function is
	 *              applied
	 * @return a composed function that first applies the {@code this_} function and
	 *         then applies the {@code after} function
	 * @throws NullPointerException if {@code this_} is null
	 * @throws NullPointerException if after is null
	 *
	 * @see #compose(ValueProvider, ValueProvider)
	 */
	public static <R, T, V> ValueProvider<T, V> compose(final ValueProvider<? super T, ? extends R> this_,
			final ValueProvider<? super R, ? extends V> after) {
		Objects.requireNonNull(this_);
		Objects.requireNonNull(after);
		return (T t) -> Optional.ofNullable(this_.apply(t)).map(after).orElse(null);
	}

	@RequiredArgsConstructor
	public static class CssTheme {
		private final String themeName;

		public <E extends HasElement> E add(E e) {
			e.getElement().getThemeList().add(themeName);
			return e;
		}

		public <E extends HasElement> E set(E e, boolean set) {
			e.getElement().getThemeList().set(themeName, set);
			return e;
		}

		public <E extends HasElement> E remove(E e) {
			e.getElement().getThemeList().remove(themeName);
			return e;
		}
	}

	@RequiredArgsConstructor
	public static class CssClass {
		private final String className;

		public <E extends HasElement> E add(E e) {
			e.getElement().getClassList().add(className);
			return e;
		}

		public <E extends HasElement> E set(E e, boolean set) {
			return set ? add(e) : remove(e);
		}

		public <E extends HasElement> E remove(E e) {
			e.getElement().getClassList().remove(className);
			return e;
		}
	}

}